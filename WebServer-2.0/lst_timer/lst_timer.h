#pragma once

#include <sys/socket.h>
#include <time.h>
#include <queue>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include "../http/http_conn.h"
#include <chrono>
#include <cstdint>
#include <vector>
#include <mutex>


struct client_data
{
    sockaddr_in address{};
    int sockfd{ -1 };
    int epollfd{ -1 };
    void(*cb_func)(client_data*) { nullptr };
    std::uint64_t token{ 0 };
};

class TimerManager
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::seconds;

    void add_timer(client_data* user, Duration ttl);
    void refresh_timer(client_data* user, Duration ttl);
    void cancel_timer(client_data* user);
    void tick();
    bool empty() const noexcept;

private:
    struct Node
    {
        TimePoint expire;
        client_data* user;
        std::uint64_t token;
    };

    struct Cmp
    {
        bool operator()(const Node& a, const Node& b)
        {
            return a.expire > b.expire;
        }
    };

    std::priority_queue<Node, std::vector<Node>, Cmp> heap_;
    std::uint64_t next_token_{ 0 };
    mutable std::mutex mtx_;
};



class Utils
{
public:
    Utils(){}
    ~Utils(){}

public:
    void init(int timeslot);

    int setnonblocking(int fd);

    void addfd(int epollfd, int fd, bool oneshot,int TRIGMOD);

    static void sig_hander(int sig);

    void addsig(int sig, void(handler)(int), bool restart = true);

    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;
    static int u_epollfd;
    int m_TIMESLOT;
    TimerManager m_timer_mgr;

};

void cb_func(client_data* user_data);