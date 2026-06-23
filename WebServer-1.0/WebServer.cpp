#include "WebServer.h"
#include "CGImysql/sql_connection_pool.h"
#include <asm-generic/socket.h>
#include <sys/socket.h>

WebServer::WebServer()
{
    // http类对象
    users = new http_conn[MAX_FD];

    // 初始化root路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    size_t len = strlen(server_path) + strlen(root) + 1;
    m_root = new char[len];
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[]users;
    delete[]m_root;
    delete m_pool;
    delete[]users_timer;
}

void WebServer::init(int port, std::string user, std::string passwd, std::string databasename, int close_log, int sql_num, int log_write, int trigmode, int thread_num, int actor_model, int opt_linger)
{
    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_databasename = databasename;
    m_close_log = close_log;
    m_sql_num = sql_num;
    m_log_write = log_write;
    m_TRIGMode = trigmode;
    m_thread_num = thread_num;
    m_actormodel = actor_model;
    m_opt_linger = opt_linger;
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passwd, m_databasename, 3306, m_sql_num, m_close_log);

    users->initmysql_result(m_connPool);
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (m_close_log == 0)
    {
        if (m_log_write == 1)
        {
            Log::getinstance()->init("./ServerLog", m_close_log, 800000, 2000, 800);
            
        }
        else
        {
            Log::getinstance()->init("./ServerLog", m_close_log, 800000, 2000, 0); 
        }
    }
}
static int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

static void addfd(int epollfd, int fd, bool one_shot, int TRIGMOD)
{
    epoll_event event;
    event.data.fd = fd;
    if (TRIGMOD == 1)
    {
        event.events = EPOLLRDHUP | EPOLLET | EPOLLIN;
    }
    else
    {
        event.events = EPOLLRDHUP | EPOLLIN;
    }

    if (one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);

}

void WebServer::adjust_timer(util_timer *timer)
{
    if (!timer)
        return;

    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_time_lst.adjust_timer(timer);
}

void WebServer::deal_timer(util_timer* timer, int sockfd)
{
    if (!timer)
        return;

    timer->cb_func(timer->user_data);
    utils.m_time_lst.del_timer(timer);
}

void WebServer::thread_pool()
{
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0)
    {
        perror("socket");
        exit(1);
    }

    if (m_opt_linger == 0)
    {
        struct linger tmp { 0, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    }
    else if (m_opt_linger == 1)
    {
        struct linger tmp { 1, 1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    }

    int reuse = 1;
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(m_listenfd);
        exit(1);
    }

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    if (bind(m_listenfd, (sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("bind");
        close(m_listenfd);
        exit(1);
    }

    if (listen(m_listenfd, SOMAXCONN) < 0)
    {
        perror("bind");
        close(m_listenfd);
        exit(1);
    }

    // epoll
    
    m_epollfd = epoll_create1(0);
    if (m_epollfd < 0)
    {
        perror("epoll_creat");
        close(m_epollfd);
        exit(1);
    }
    addfd(m_epollfd, m_listenfd, false, m_LISTENTTrigmode);

    http_conn::m_epollfd = m_epollfd;
    Utils::u_epollfd = m_epollfd;
    Utils::u_pipefd = m_pipefd;

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.setnonblocking(m_pipefd[1]);
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_hander, false);
    utils.addsig(SIGTERM, utils.sig_hander, false);

    alarm(TIMESLOT);

}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log);

    // 初始化client_data，创建定时器并加入链表
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer* timer = new util_timer;
    timer->cb_func = cb_func;
    timer->user_data = &users_timer[connfd];
    users_timer[connfd].timer = timer;

    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_time_lst.add_timer(timer);

}

bool WebServer::dealwithclientdata()
{
    sockaddr_in client_address;
    socklen_t addr_client_length = sizeof(client_address);
    if (m_LISTENTTrigmode == 0)
    {
        int connfd = accept(m_listenfd, (sockaddr*)&client_address, &addr_client_length);
        LOG_INFO("accept client fd=%d", connfd);
        if (connfd < 0)
        {
            perror("accept");
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD)
        {
            return false;
        }

        timer(connfd, client_address);
        return true;
    }
    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (sockaddr*)&client_address, &addr_client_length);
            if (connfd < 0)
            {
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                break;
            }

            timer(connfd, client_address);
        }
        
        return false;
    }

    return true;
}

bool WebServer::dealwithread(int sockfd)
{
    util_timer* timer = users_timer[sockfd].timer;

    // reactor
    if (m_actormodel == 1)
    {
        if (timer)
            adjust_timer(timer);

        m_pool->append(users + sockfd, 0);   //这里没检查失败情况,其实很危险

        while (true)   //忙等设计,很丑陋,后续可优化
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(users_timer[sockfd].timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }

                users[sockfd].improv = 0;
                break;
            }
        }

    }
    else
    {
        // proactor   
        if (users[sockfd].read_once())
        {
            
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }

    return true;
}

bool WebServer::dealwithwrite(int sockfd)
{
    util_timer* timer = users_timer[sockfd].timer;

// reactor
    if (m_actormodel == 1)
    {
        if (timer)
            adjust_timer(timer);

        m_pool->append(users + sockfd, 1);   //这里没检查失败情况,其实很危险

        while (true)   //忙等设计,很丑陋,后续可优化
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(users_timer[sockfd].timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }

                users[sockfd].improv = 0;
                break;
            }
        }

    }
    else
    {
        // proactor   
        if (users[sockfd].write())
        {
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }

    return true;
}

bool WebServer::dealwithsignal(bool& timeout, bool& stop_server)
{
    int ret = 0;
    char msg[1024];
    ret = recv(m_pipefd[0], msg, sizeof(msg), 0);
    if (ret == -1)
        return false;
    else if (ret == 0)
        return false;
    else
    {
        for (int i = 0 ; i < ret; ++i)
        {
            switch (msg[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }

            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }

    return true;

}

void WebServer::eventLoop()
{
    bool timeout=false;
    bool stop_server=false;

    while(!stop_server)
    {
        int number=epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);

        for(int i=0; i<number; ++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==m_listenfd)
            {
                if (!dealwithclientdata()) continue;;
            }
            else if(events[i].events &(EPOLLHUP|EPOLLRDHUP|EPOLLERR))
            {
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            else if(sockfd==m_pipefd[0] && (events[i].events & EPOLLIN))
            {
                bool flags = dealwithsignal(timeout, stop_server);
            }
            else if(events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if(events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if(timeout)
        {
            utils.timer_handler();
            timeout = false;
        }
    }
}