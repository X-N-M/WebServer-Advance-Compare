#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string>
#include "block_queue.h"
#include "../lock/lock.h"
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <array>
#include <vector>
#include <chrono>
#include <filesystem>
#include <ctime>
#include <thread>
#include <atomic>

class Log
{

public:

    struct LogItem
    {
    std::string line;
    std::tm tm;
    };

    static Log* getinstance()
    {
        static Log instance;
        return &instance;
    }

    

    bool init(const char* file_name, int close_log, int splite_lines = 5000000, int log_buf_size = 8192, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush();

private:

    Log();
    ~Log();
    void async_write_log()
    {
        std::vector<LogItem> batch;
        batch.reserve(64);
        LogItem tmp;

        while (m_log_queue->pop(tmp))
        {
            batch.clear();
            batch.push_back(std::move(tmp));

            while (batch.size() < 64 && m_log_queue->try_pop(tmp))
            {
                batch.push_back(std::move(tmp));
            }
            
            m_mutex.lock();
            for (const auto& it : batch)
            {
                write_item_to_file(it);
            }
            m_mutex.unlock();

            flush();
        }
        return;
    }

    void write_item_to_file(const LogItem& tmp);
    std::filesystem::path make_log_path(const std::tm& my_tm, long long index)const;
    static std::string format_day(const std::tm& my_tm);

private:

    std::filesystem::path m_log_root; //目录名
    std::string m_log_name; //日志文件名
    std::string m_current_day;
    char* m_buf; //共享缓冲区
    int m_log_buf_size;  //日志缓冲区大小
    int m_splite_lines; //日志最大行数
    long long m_file_index = 0;  //日志行数
    long long m_lines_in_current_file = 0;
    FILE* m_fp;
    block_queue<LogItem>* m_log_queue;
    locker m_mutex;
    int m_mytoday; //今日时间
    bool m_is_async;
    int m_close_log; //关闭日志
    std::mutex queue_mtx;
    std::condition_variable queue_cv;

    std::thread writer_;
    std::atomic<bool> running_{ false };
};

#define LOG_DEBUG(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(0, format, ##__VA_ARGS__);}
#define LOG_INFO(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(1, format, ##__VA_ARGS__); }
#define LOG_WARN(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(2, format, ##__VA_ARGS__);}
#define LOG_ERROR(format, ...) if(m_close_log==0) {Log::getinstance()->write_log(3, format, ##__VA_ARGS__); }

