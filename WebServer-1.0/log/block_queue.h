#pragma once

#include <pthread.h>
#include <semaphore>
#include "../lock/lock.h"
#include <sys/time.h>
#include <iostream>

template<class T>
class block_queue
{
public:

    block_queue(int max_size = 1000) :m_max_size(max_size), m_size(0), m_front(-1), m_back(-1)
    {
        m_array = new T[m_max_size];
    }

    ~block_queue()
    {
        m_lock.lock();
        delete[]m_array;
        m_lock.unlock();
    }

    void clear()
    {
        m_lock.lock();

        m_size = 0;
        m_back = -1;
        m_front = -1;

        m_lock.unlock();
    }

    bool push(const T &item)
    {
        m_lock.lock();

        if (m_size >= m_max_size)
        {
            m_cond.broadcast();  //语义上其实没什么必要
            m_lock.unlock();
            return false;
        }


        m_back = (m_back + 1) % m_max_size;
        ++m_size;

        m_array[m_back] = item;

        m_cond.broadcast();
        m_lock.unlock();
        return true;
    }

    bool pop(T& value)
    {
        m_lock.lock();

        while (m_size <= 0)
        {
            if (!m_cond.wait(m_lock.get()))
            {
                m_lock.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        --m_size;
        value = m_array[m_front];

        m_lock.unlock();
        return true;
    }

    bool pop(T& value, int ms_timeout)
    {
        timespec t = { 0, 0 };
        timeval now = { 0,0 };
        gettimeofday(&now, NULL);
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000000;

        m_lock.lock();
         
        while(m_size <= 0)
        {
            if (!m_cond.timewait(m_lock.get(), t))
            {
                m_lock.unlock();
                return false;
            }
        }

        // if (m_size <= 0)
        // {
        //     m_lock.unlock();
        //     return false;
        // }

        m_front = (m_front + 1) % m_max_size;
        --m_size;

        value = m_array[m_front];

        m_lock.unlock();
        return true;
    }

    bool full()
    {
        m_lock.lock();

        bool flag = (m_size == m_max_size);

        m_lock.unlock();

        return flag;
    }

    bool empty()
    {
        m_lock.lock();

        bool flag = (m_size == 0);

        m_lock.unlock();

        return flag;
    }

private:

    T* m_array;
    locker m_lock;
    cond m_cond;

    int m_max_size;
    int m_size;
    int m_front;
    int m_back;
};