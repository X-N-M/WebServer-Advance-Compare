#include "http_conn.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mysql/mysql.h>
#include <fstream>
#include <map>
#include <string>

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found in this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
std::map<std::string, std::string> users;

void http_conn::initmysql_result(connection_pool* connPool)
{
    MYSQL* mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username, passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    MYSQL_RES* result = mysql_store_result(mysql);

    int num_fields = mysql_num_fields(result);

    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string tmp1(row[0]);
        std::string tmp2(row[1]);
        users[tmp1] = tmp2;
    }
}

http_conn::http_conn()
{
    m_sockfd = -1;
    doc_root = nullptr;
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_string = nullptr;
    m_file_address = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_read_idx = 0;
    m_check_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    m_content_length = 0;
    m_linger = false;
    m_TRIGMOD = 0;
    cgi = 0;
}

http_conn::~http_conn()
{

}

// 设置非阻塞模式，防呆设计
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 向内核事件表注册事件，选择开启oneshot模式，注册读事件
void addfd(int epollfd, int fd, int TRIGMOD, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMOD == 1)
    {
        event.events = EPOLLRDHUP | EPOLLIN | EPOLLET;
    }
    else
    {
        event.events = EPOLLRDHUP | EPOLLIN;
    }

    if (one_shot) event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 重新设置事件的oneshot模式
void modfd(int epollfd, int fd, int ev, int TRIGMOD)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMOD == 1)
    {
        event.events = ev | EPOLLRDHUP | EPOLLET | EPOLLONESHOT;
    }
    else
    {
        event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
// 从内核事件表删除该文件描述符，释放fd
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 初始化链接
void http_conn::init(int sockfd, const sockaddr_in& addr, const char* root, int TRIGMOD, int close_log, int epollfd)

{
    m_sockfd = sockfd;
    m_address = addr;
    doc_root = root;
    m_TRIGMOD = TRIGMOD;
    m_close_log = close_log;
    m_epollfd = epollfd;
    addfd(m_epollfd, m_sockfd, m_TRIGMOD, true);
    m_user_count.fetch_add(1, std::memory_order_relaxed);

    init();
}
// 初始化新接受的链接
void http_conn::init()
{
    m_write_idx = 0;

    m_read_idx = 0;
    m_check_idx = 0;
    m_check_state = CHECK_REQUEST_LINE;
    m_start_line = 0;

    bytes_have_send = 0;
    bytes_to_send = 0;

    m_version = 0;
    m_url = 0;
    m_host = 0;
    m_linger = false;
    m_content_length = 0;

    m_method = GET;
    cgi = 0;



    memset(m_read_buf, '\0', READ_BUF_SIZE);
    memset(m_write_buf, '\0', WRITE_BUF_SIZE);
    memset(m_real_file, '\0', FILE_NAME_LENTH);
}

std::atomic<int> http_conn::m_user_count{ 0 };

void http_conn::close_conn(bool real_close)
{
    if (real_close && m_sockfd != -1)
    {
        // 错误日志占位
        removefd(m_epollfd, m_sockfd);
        m_user_count.fetch_sub(1, std::memory_order_relaxed);
        m_sockfd = -1;
    }
}

bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUF_SIZE) return false;

    int read_bytes = 0;
    // LT模式读取
    if (m_TRIGMOD == 0)
    {
        read_bytes = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
        if (read_bytes <= 0) return false;

        m_read_idx += read_bytes;
        return true;
    }
    else
    {
        // ET模式循环读取
        while (1)
        {
            read_bytes = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);

            if (read_bytes == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if (read_bytes == 0) return false;

            m_read_idx += read_bytes;
        }
        return true;
    }
}

// 对请求信息进行“切行”
http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    for (; m_check_idx < m_read_idx; ++m_check_idx)
    {
        tmp = m_read_buf[m_check_idx];
        if (tmp == '\r')
        {
            if (m_check_idx+1==m_read_idx)
            {
                return LINE_INCOMPLETE;
            }
            else if(m_read_buf[m_check_idx+1]=='\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (m_check_idx > 1 && m_read_buf[m_check_idx-1] == '\r')
            {
                m_read_buf[m_check_idx-1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            else return LINE_BAD;
        }

    }

    return LINE_INCOMPLETE;
}
// 解析请求行，获得头部方法及版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url) return BAD_REQUEST;

    *m_url++ = '\0';
    m_url += strspn(m_url, " \t");

    char* method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else return BAD_REQUEST;

    m_version = strpbrk(m_url, " \t");
    if (!m_version) return BAD_REQUEST;

    *m_version++ = 0;
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    if (strncasecmp(m_url, "http://", 7)==0)
    {
        m_url += 7;
        m_url=strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8)==0)
    {
        m_url += 8;
        m_url=strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') return BAD_REQUEST;
    if (strlen(m_url) == 1)
    {
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_REQUEST_HEADER;

    return NO_REQUEST;
}
// 解析头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_REQUEST_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "HOST:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else
    {
        // 日志占位
    }

    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= m_content_length + m_check_idx)
    {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    char* text;
    HTTP_CODE ret = NO_REQUEST;
    LINE_STATUS line_status = LINE_OK;

    while ((m_check_state == CHECK_REQUEST_CONTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK)
    {
        text = getline();
        m_start_line = m_check_idx;
        switch (m_check_state)
        {
        case CHECK_REQUEST_LINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            break;
        }
        case CHECK_REQUEST_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            else if (ret == GET_REQUEST) return do_request();

            break;
        }
        case CHECK_REQUEST_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST) return do_request();

            line_status = LINE_INCOMPLETE;
            break;
        }

        default:
            return INTERNAL_ERROR;
        }

    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    const char *p = strrchr(m_url, '/');
    
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char name[100], passwd[100];
        int i = 5;
        for (; m_string[i] != '&'; ++i)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for (i=i+10; m_string[i] != '\0'; ++i, ++j)
        {
            passwd[j] = m_string[i];
        }
        passwd[j] = '\0';

        if (*(p + 1) == '3')
        {
            char sql_insert[200];
            snprintf(sql_insert, 200, "INSERT INTO user(username, passwd) VALUES('%s', '%s')", name, passwd);

            m_lock.lock();
            if (users.find(name) == users.end())
            {
                int res = mysql_query(mysql, sql_insert);
                if (!res)
                {
                    users.insert(std::make_pair<std::string, std::string>(name, passwd));
                    strcpy(m_url, "/log.html");
                }
                else
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }
            m_lock.unlock();
        }
        else if (*(p + 1) == '2')
        {
            m_lock.lock();
            if (users.find(name) != users.end() && users[name] == passwd)
            {
                strcpy(m_url, "/welcome.html");
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
            m_lock.unlock();
        }

        
    }
    
    if (*(p + 1) == '0')
    {
        strcpy(m_url, "/register.html");
    }
     else if (*(p + 1) == '1')
    {
        strcpy(m_url, "/log.html");
    }
    else if (*(p + 1) == '5')
    {
        strcpy(m_url, "/picture.html");
    }
    else if (*(p + 1) == '6')
    {
        strcpy(m_url, "/video.html");
    }
    else if (*(p + 1) == '7')
    {
        strcpy(m_url, "/fans.html");
    }

    snprintf(m_real_file, sizeof(m_real_file), "%s%s", doc_root, m_url);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUF_SIZE)
        return false;
    
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUF_SIZE - m_write_idx - 1, format, arg_list);
    if (len >= WRITE_BUF_SIZE - m_write_idx - 1)
    {
        va_end(arg_list);
        return false;
    }

    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
            return false;
        break;
    }
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        }
        else
        {
            const char* ok_string = "<html><body></body></html>";
            if (!add_content(ok_string))
                return false;
            
        }
        break;
    }
    default:
    {
        return false;
    }

    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;

    bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::write()
{
    int tmp = 0;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMOD);
        init();
        return true;
    }

    while (1)
    {   
        
        tmp = writev(m_sockfd, m_iv, m_iv_count);
      
        if (tmp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMOD);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += tmp;
        bytes_to_send -= tmp;

        if (bytes_have_send >= m_write_idx)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_write_idx - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMOD);
            if (m_linger)
            {   
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMOD);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
        return;
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMOD);
}