#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string>
#include "block_queue.h"
#include "../lock/lock.h"
#include <iostream>


class Log
{

public:

    static Log* getinstance()
    {
        static Log instance;
        return &instance;
    }

    static void* flush_log_thread(void*)
    {
        Log::getinstance()->async_write_log();
        return NULL;
    }

    bool init(const char* file_name, int close_log, int splite_lines = 5000000, int log_buf_size = 8192, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush();

private:

    Log();
    ~Log();
    void* async_write_log()
    {
        std::string tmp;

        while (m_log_queue->pop(tmp))
        {
            m_mutex.lock();
            fputs(tmp.c_str(), m_fp);
            m_mutex.unlock();
        }
        return NULL;
    }

private:

    char m_dir_name[128]; //目录名
    char m_log_name[128]; //日志文件名
    char* m_buf; //共享缓冲区
    int m_log_buf_size;  //日志缓冲区大小
    int m_splite_lines; //日志最大行数
    long long m_count;  //日志行数
    FILE* m_fp;
    block_queue<std::string>* m_log_queue;
    locker m_mutex;
    int m_mytoday; //今日时间
    bool m_is_async;
    int m_close_log; //关闭日志

};

#define LOG_DEBUG(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(0, format, ##__VA_ARGS__);}
#define LOG_INFO(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(1, format, ##__VA_ARGS__); }
#define LOG_WARN(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(2, format, ##__VA_ARGS__);}
#define LOG_ERROR(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(3, format, ##__VA_ARGS__); }
