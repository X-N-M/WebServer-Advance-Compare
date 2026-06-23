#pragma once

#include <list>
#include <mysql/mysql.h>
#include "../lock/lock.h"
#include "../log/log.h"
#include <cstring>
#include <string>

class connection_pool
{
public:

    MYSQL* GetConnection();
    int GetFreeConn();
    bool ReleaseConnection(MYSQL* con);
    void DestroyPool();

    static connection_pool* GetInstance();

    void init(std::string url, std::string user_name, std::string PassWord, std::string DataBasename, int port, int Maxconn, int close_log);

private:
    connection_pool();
    ~connection_pool();

    int m_Maxconn; //最大连接数
    int m_Curconn;  //使用数
    int m_freeconn; //空闲数
    std::list<MYSQL*> connList; //数据连接池
    sem reserve;
    locker lock;

public:
    std::string m_url;
    std::string m_user_name;
    std::string m_passwd;
    std::string m_DataBasename;
    int m_port;
    int m_close_log;
};

class connectionRAII
{
public:
    connectionRAII(MYSQL** con, connection_pool* connpool);
    ~connectionRAII();

private:
    MYSQL* conRAII;
    connection_pool* poolRAII;
};