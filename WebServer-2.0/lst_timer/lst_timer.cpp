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
    m_timer_mgr.tick();
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

void TimerManager::add_timer(client_data* user, Duration ttl)
{
    if (!user)
    {
        return;
    }

    std::lock_guard<std::mutex> lk(mtx_);
    user->token = ++next_token_;
    heap_.push(Node{ Clock::now() + ttl, user, user->token });
}

void TimerManager::refresh_timer(client_data *user, Duration ttl)
{
    add_timer(user, ttl);
}

void TimerManager::cancel_timer(client_data* user)
{
    if (!user)
    {
        return;;
    }

    std::lock_guard<std::mutex> lk(mtx_);
    user->token = 0;
}

bool TimerManager::empty() const noexcept
{
    std::lock_guard<std::mutex> lk(mtx_);
    return heap_.empty();
}

void TimerManager::tick()
{
    const auto now = Clock::now();

    while (true)
    {
        client_data* user = nullptr;
        void(*cb)(client_data*) = nullptr;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (heap_.empty())
            {
                break;
            }
            
            Node node = heap_.top();
            if (node.token != node.user->token)
            {
                heap_.pop();
                continue;
            }

            if (node.expire > now)
            {
                break;
            }

            heap_.pop();
            node.user->token = 0;
            user = node.user;
            cb = user->cb_func;
        }

        if (cb)
        {
            cb(user);
        }
    }
}

void Utils::show_error(int connfd, const char* info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}



int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data* user_data)
{
    assert(user_data);
    epoll_ctl(user_data->epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}


