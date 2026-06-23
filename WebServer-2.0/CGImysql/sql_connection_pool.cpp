#include "sql_connection_pool.h"
#include <mysql/mysql.h>

connection_pool::connection_pool()
{
    m_freeconn = 0;
    m_Curconn = 0;
}

connection_pool* connection_pool::GetInstance()
{
    static connection_pool conn_pool;
    return &conn_pool;
}

void connection_pool::init(std::string url, std::string user_name, std::string PassWord, std::string DataBasename, int port, int Maxconn, int close_log)
{
    m_port = port;
    m_close_log = close_log;
    m_url = url;
    m_user_name = user_name;
    m_DataBasename = DataBasename;
    m_passwd = PassWord;
    m_Maxconn = Maxconn;

    for (int i = 0; i < Maxconn; ++i)
    {
        MYSQL* con = NULL;
        con = mysql_init(con);

        if (con == NULL)
        {
            LOG_ERROR("mysql error");
            exit(1);
        }

        con = mysql_real_connect(con, m_url.c_str(), m_user_name.c_str(), m_passwd.c_str(), m_DataBasename.c_str(), m_port, NULL, 0);

        if (con == NULL)
        {
            LOG_ERROR("mysql connect error");
            exit(1);
        }

        connList.push_back(con);
        ++m_freeconn;
    }

    reserve = sem(m_freeconn);
}

int connection_pool::GetFreeConn()
{
    return this->m_freeconn;
}

MYSQL* connection_pool::GetConnection()
{
    MYSQL* con = NULL;
    reserve.wait();

    lock.lock();

    con = connList.front();
    connList.pop_front();
    --m_freeconn;
    ++m_Curconn;

    lock.unlock();

    return con;
}

bool connection_pool::ReleaseConnection(MYSQL* con)
{
    if (con == NULL)
    {
        return false;
    }

    lock.lock();

    connList.push_back(con);
    ++m_freeconn;
    --m_Curconn;

    lock.unlock();

    reserve.post();
    return true;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if (connList.size())
    {
            
        for (auto item : connList)
        {
            mysql_close(item);
        }

        m_Curconn = 0;
        m_freeconn = 0;
        connList.clear();

    }   
    lock.unlock();

}

connection_pool::~connection_pool()
{
    this->DestroyPool();
}

connectionRAII::connectionRAII(MYSQL** con, connection_pool* pool)
{
    *con = pool->GetConnection();

    conRAII = *con;
    poolRAII = pool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}