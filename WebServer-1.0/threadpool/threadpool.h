#pragma once

#include "../lock/lock.h"
#include <pthread.h>
#include <list>
#include "../CGImysql/sql_connection_pool.h"

template<class T>

class threadpool
{
public:
    
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();

    bool append(T* request, int stat);
    bool append_p(T* request);

private:
    static void* work(void* arg);
    void run();

private:

    int m_thread_number;
    int m_max_requests;
    std::list<T* > m_workqueue; // 请求队列
    locker m_queuelocker;
    sem m_queuestat; //是否有任务需要处理
    pthread_t* threads_id; //描述线程池数组
    connection_pool* m_connPool; //数据库
    int m_actor_model; //模型切换

};

template<class T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number , int max_requests) :m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_connPool(connPool), threads_id(NULL)
{
    if (thread_number < 0 || max_requests < 0)
        throw std::exception();
    threads_id = new pthread_t[m_thread_number];
    if (!threads_id)
        throw std::exception();

    for (int i = 0; i < m_thread_number; ++i)
    {
        if (pthread_create(threads_id + i, NULL, work, this) != 0)
        {
            delete[]threads_id;
            throw std::exception();
        }

        if (pthread_detach(threads_id[i]))
        {
            delete[]threads_id;
            throw std::exception();
        }
    }

}

template<class T>
threadpool<T>::~threadpool()
{
    delete[]threads_id;
}

template<class T>
bool threadpool<T>::append(T* request, int stat)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = stat;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}


template<class T>
bool threadpool<T>::append_p(T* request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<class T>
void* threadpool<T>::work(void* arg)
{
    threadpool* tmp = (threadpool*)arg;
    tmp->run();
}

template<class T>
void threadpool<T>::run()
{
    while (1)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (m_actor_model == 1)
        {
            if (request->m_state == 0)
            {
                if (request->read_once())
                {
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }

            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }

            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            
            request->process();
      
            
        }
    }
}
