#include "lst_timer.h"
// 设置文件描述符为非阻塞
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool oneshot, int TRIGMOD)
{
    epoll_event events;
    events.data.fd = fd;
    if (TRIGMOD == 1)
    {
        events.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    }
    else
    {
        events.events = EPOLLIN | EPOLLRDHUP;
    }
    if (oneshot)
        events.events |= EPOLLONESHOT;

    setnonblocking(fd);
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &events);

}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);

}

// 检查超时，设置闹钟
void Utils::timer_handler()
{
    m_time_lst.tick();
    alarm(m_TIMESLOT);
}

// 信号处理函数
void Utils::sig_hander(int sig)
{
    int save_errno = errno;
    // 这里其实没有考虑send失败的情况
    send(u_pipefd[1], &sig, 1, 0);
    errno = save_errno;
}

void Utils::show_error(int connfd, const char* info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

sort_timer_lst::sort_timer_lst() :head(NULL), tail(NULL)
{

}

sort_timer_lst::~sort_timer_lst()
{
    util_timer* tmp = head;
    while (tmp)
    {
        head = head->next;
        if (head)
            head->prev = NULL;

        delete tmp;
        tmp = head;
    }

    tail = NULL;
}

void sort_timer_lst::add_timer(util_timer* timer)
{
    if (!timer)
        return;

    if (!head)
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }

    add_timer(timer, head);
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head)
{
    util_timer* prev=lst_head;
    util_timer* tmp = prev->next;

    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            tmp->prev->next = timer;
            timer->prev = tmp->prev;
            tmp->prev = timer;
            timer->next = tmp;
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    prev->next = timer;
    timer->prev = prev;
    timer->next = NULL;
    tail = timer;

}

void sort_timer_lst::adjust_timer(util_timer* timer)
{
    if (!timer)
        return;

    util_timer* tmp = timer->next;
    if (!tmp || timer->expire < tmp->expire)
        return;

    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        timer->next = NULL;
        timer->prev = NULL;
        add_timer(timer, head);
    }
}

void sort_timer_lst::del_timer(util_timer* timer)
{
    if (!timer)
        return;

    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }

    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }

    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    timer->next->prev = timer->prev;
    timer->prev->next = timer->next;
    delete timer;
    return;
}

void sort_timer_lst::tick()
{
    time_t cur = time(NULL);
    util_timer* tmp = head;
    while (tmp)
    {
        if (cur < tmp->expire)
            return;

        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        delete tmp;
        if (head)
            head->prev = NULL;
        tmp = head;
    }

    tail = NULL;
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data* user_data)
{
    assert(user_data);
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}