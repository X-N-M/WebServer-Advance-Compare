#pragma once

#include <sys/socket.h>
#include <time.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include "../http/http_conn.h"

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer* timer;
};

class util_timer
{
public:
    util_timer() :next(NULL), prev(NULL) {}

    time_t expire;
    void(*cb_func)(client_data*);
    util_timer* next;
    util_timer* prev;
    client_data* user_data;
};

class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

public:
    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void tick();

private:
    void add_timer(util_timer* timer, util_timer* lst_head);

    util_timer* head;
    util_timer* tail;
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
    sort_timer_lst m_time_lst;
    int m_TIMESLOT;

};

void cb_func(client_data* user_data);