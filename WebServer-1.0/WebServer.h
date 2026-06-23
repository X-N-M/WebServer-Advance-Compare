#pragma once

#include "./http/http_conn.h"
#include <string>
#include <sys/epoll.h>
#include <cstdio>
#include <stdlib.h>
#include <unistd.h>
#include "./lst_timer/lst_timer.h"
#include "./threadpool/threadpool.h"
#include "./CGImysql/sql_connection_pool.h"

const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int MAX_FD = 65536;    //最大事件数
const int TIMESLOT = 5;     //最小超时单位

class WebServer
{
public:

    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string passwd, std::string databasename, int m_close_log, int sql_num, int log_write, int trigmode, int thread_num, int actor_model, int opt_linger);
    void eventListen();
    void eventLoop();

    bool dealwithclientdata();
    bool dealwithread(int sockfd);
    bool dealwithwrite(int sockfd);
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void timer(int connfd, struct sockaddr_in client_address);

    void thread_pool();
    void adjust_timer(util_timer* timer);
    void deal_timer(util_timer* timer, int sockfd);
    void sql_pool();
    void log_write();
    void trig_mode();

public:
    // 基础
    int m_epollfd;
    int m_pipefd[2];
    int m_port;
    char* m_root;
    int m_actormodel;
    int m_close_log;
    int m_log_write;
    int m_opt_linger;

    http_conn* users;

    //数据库相关
    connection_pool* m_connPool;
    std::string m_user;
    std::string m_passwd;
    std::string m_databasename;
    int m_sql_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_TRIGMode;
    int m_CONNTrigmode;
    int m_LISTENTTrigmode;

    // 定时器相关

    client_data* users_timer;
    Utils utils;

    // 线程池相关
    threadpool<http_conn>* m_pool;
    int m_thread_num;


};