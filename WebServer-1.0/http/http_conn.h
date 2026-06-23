#pragma once

#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <errno.h>
#include <sys/mman.h>
#include <cstdio>
#include <stdarg.h>
#include <sys/uio.h>
#include "../lock/lock.h"
#include "../CGImysql/sql_connection_pool.h"


class http_conn
{
private:
    /* data */
public:

    static const int READ_BUF_SIZE=2048;
    static const int WRITE_BUF_SIZE=1024;
    static const int FILE_NAME_LENTH=200;

    // HTTP请求方法
    enum METHOD
    {
        GET=0,
        POST,
    };

    enum CHECK_STATE
    {
        CHECK_REQUEST_LINE,
        CHECK_REQUEST_HEADER,
        CHECK_REQUEST_CONTENT,
    };

    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FILE_REQUEST,
        FORBIDDEN_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    enum LINE_STATUS
    {
        LINE_OK,
        LINE_BAD,
        LINE_INCOMPLETE,
    };

public:
    
    void init(int sockfd, const sockaddr_in& addr, const char* root, int TRIGMOD, int close_log);
    void initmysql_result(connection_pool* connPool);
    void close_conn(bool real_close = true);

    bool read_once();
    bool write();
    void process();

    sockaddr_in* get_address() { return &m_address; }

    int timer_flag;
    int improv;

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL* mysql;
    int m_state;

public:
    
    http_conn();
    ~http_conn();

private:


    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);

    // 解析请求信息
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();

    LINE_STATUS parse_line();
    char* getline() { return m_read_buf + m_start_line; }

    void unmap();

    // 写响应信息的辅助函数
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);

private:

    int m_sockfd;
    sockaddr_in m_address;

    // 将请求信息读入缓冲区相关变量
    char m_read_buf[READ_BUF_SIZE];
    int m_read_idx;
    int m_check_idx;
    int m_start_line;

    // 将响应信息写入缓冲区相关变量
    char m_write_buf[WRITE_BUF_SIZE];
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    char m_real_file[FILE_NAME_LENTH];
    const char* doc_root;

    // 存储请求信息各属性
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;
    char* m_string;

    char* m_file_address;
    struct stat m_file_stat;

    struct iovec m_iv[2];
    int m_iv_count;

    int bytes_to_send;
    int bytes_have_send;

    int m_TRIGMOD;
    int cgi;

    // 日志
    int m_close_log;


};