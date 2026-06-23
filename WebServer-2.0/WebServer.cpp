#include "WebServer.h"
#include "CGImysql/sql_connection_pool.h"
#include "lst_timer/lst_timer.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <sys/epoll.h>
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
    shutdown_sub_reactors();

    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);

    delete[]users;
    delete[]m_root;
    delete m_pool;
    delete[]users_timer;
}

void WebServer::init(int port, std::string user, std::string passwd, std::string databasename, int close_log, int sql_num, int log_write, int trigmode, int thread_num, int opt_linger)
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
        Log::getinstance()->init("./ServerLog", m_close_log, 800000, 2000, 800);
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

void WebServer::adjust_timer(client_data *user)
{
    if (!user)
        return;

    utils.m_timer_mgr.refresh_timer(user, std::chrono::seconds{ 3 * TIMESLOT });
}

void WebServer::deal_timer(client_data *user, int sockfd)
{
    if (!user)
        return;
    utils.m_timer_mgr.cancel_timer(user);
    if (user->cb_func)
        user->cb_func(user);
}
void WebServer::init_sub_reactors(size_t n)
{
    m_sub_reactors.reserve(n);

    for (size_t i=0; i < n; ++i)
    {
        auto sub = std::make_unique<SubReactor>();
        sub->owner = this;
        sub->epollfd = epoll_create1(EPOLL_CLOEXEC);
        sub->wakefd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (sub->epollfd < 0 || sub->wakefd < 0)
        {
            perror("epoll_creat/eventfd");
            exit(1);
        }

        epoll_event ev{};
        ev.data.fd = sub->wakefd;
        ev.events = EPOLLIN;
        epoll_ctl(sub->epollfd, EPOLL_CTL_ADD, sub->wakefd, &ev);

        m_sub_reactors.push_back(std::move(sub));
        m_sub_reactors.back()->start();
    }
}

WebServer::SubReactor& WebServer::pick_sub_reactor()
{
    return *m_sub_reactors[m_next_sub.fetch_add(1, std::memory_order_relaxed)% m_sub_reactors.size()] ;
}

void WebServer::SubReactor::loop()
{
    epoll_event events[MAX_EVENT_NUMBER];
    while (!stop.load(std::memory_order_acquire))
    {
        int n = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (stop.load(std::memory_order_acquire))
            {
                break;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == wakefd)
            {
                uint64_t v;
                ::read(wakefd, &v, sizeof(v));
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                owner->deal_timer(&owner->users_timer[fd], fd);
                continue;
            }

            if (events[i].events & EPOLLIN)
            {
                owner->dealwithread(fd);
            }

            if (events[i].events & EPOLLOUT)
            {
                owner->dealwithwrite(fd);
            }
        }
    }
}

void WebServer::shutdown_sub_reactors()
{
    for (auto& sub : m_sub_reactors)
    {
        sub->request_stop();
    }

    for (auto& sub : m_sub_reactors)
    {
        sub->join();
        close(sub->epollfd);
        close(sub->wakefd);
    }

    m_sub_reactors.clear();
}

void WebServer::thread_pool()
{
    m_pool = new threadpool<http_conn>(m_connPool, m_thread_num);
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

void WebServer::timer(int connfd, struct sockaddr_in client_address, int epollfd)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, epollfd);

    // 初始化client_data，创建定时器并加入链表
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    users_timer[connfd].cb_func = cb_func;
    users_timer[connfd].epollfd = epollfd;

    utils.m_timer_mgr.add_timer(&users_timer[connfd], std::chrono::seconds{ 3 * TIMESLOT });


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
        if(http_conn::m_user_count.load(std::memory_order_relaxed) >= MAX_FD||connfd >= MAX_FD)
        {
            close(connfd);
            return false;
        }

        auto& sub = pick_sub_reactor();
        timer(connfd, client_address, sub.epollfd);
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
            if (http_conn::m_user_count.load(std::memory_order_acquire) >= MAX_FD||connfd >= MAX_FD)
            {
                close(connfd);
                continue;
            }

            auto& sub = pick_sub_reactor();
            timer(connfd, client_address, sub.epollfd);
        }
        
        return false;
    }

    return true;
}

bool WebServer::dealwithread(int sockfd)
{
    client_data* timer = &users_timer[sockfd];

    // reactor
    if (!users[sockfd].read_once())
    {
        deal_timer(&users_timer[sockfd], sockfd);
        return false;
    }

    if (!m_pool->append_p(users + sockfd))
    {
        static constexpr char resp[] = "HTTP/1.1 503 Service Unavaliable\r\n"
            "Connection: close\r\n"
            "Content-Length:0\r\n\r\n";

        send(sockfd, resp, sizeof(resp) - 1, 0);
        deal_timer(&users_timer[sockfd], sockfd);
        return false;
    }

    adjust_timer(&users_timer[sockfd]);
    
    return true;
}

bool WebServer::dealwithwrite(int sockfd)
{
    client_data* timer = &users_timer[sockfd];

// reactor

    if (!users[sockfd].write())
    {
        deal_timer(&users_timer[sockfd], sockfd);
        return false;
    }

    
    if (timer) adjust_timer(timer);

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
                client_data* timer = &users_timer[sockfd];
                deal_timer(timer, sockfd);
            }
            else if(sockfd==m_pipefd[0] && (events[i].events & EPOLLIN))
            {
                bool flags = dealwithsignal(timeout, stop_server);
            }
            
        }
        if(timeout)
        {
            utils.timer_handler();
            timeout = false;
        }
    }
}