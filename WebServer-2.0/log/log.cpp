#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <pthread.h>
#include <stdarg.h>
#include "log.h"
#include <string>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

Log::Log()
{
    m_buf = nullptr;
    m_log_queue = nullptr;
    m_is_async = false;
}

Log::~Log()
{
    running_.store(false);

    if (m_log_queue)
    {
        m_log_queue->close();
    }

    if (writer_.joinable())
    {
        writer_.join();
    }

    if (m_fp)
    {
        flush();
        std::fclose(m_fp);
        m_fp = nullptr;
    }

    delete[] m_log_queue;
    m_log_queue = nullptr;

    delete[] m_buf;
    m_buf = nullptr;

}

bool Log::init(const char* file_name, int close_log, int splite_lines, int log_buf_size, int max_queue_size)
{
    
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_splite_lines = splite_lines;
    
    auto now = std::chrono::system_clock::now();
    auto tt=std::chrono::system_clock::to_time_t(now);
    std::tm my_tm{};
    localtime_r(&tt, &my_tm);
    
    std::filesystem::path base(file_name);
    if (base.has_parent_path())
    {
        std::filesystem::create_directories(base.parent_path());
    }
    m_log_root = base.has_parent_path() ? base.parent_path() : std::filesystem::path(".");
    m_log_name = base.filename().string();
    
    m_current_day = format_day(my_tm);
    m_file_index = 0;
    m_lines_in_current_file = 0;
    
    auto log_full_name = make_log_path(my_tm, 0);
    
    m_mytoday = my_tm.tm_mday;
    m_fp = fopen(log_full_name.string().c_str(), "a");
    if (m_fp == NULL) {
        return false;
    }
    
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<LogItem>(max_queue_size);
        running_.store(true);
        writer_ = std::thread(&Log::async_write_log, this);
    }
    else
    {
        return false;
    } 
    return true;

}

void Log::flush()
{
  m_mutex.lock();
  fflush_unlocked(m_fp);
  m_mutex.unlock();
}

void Log::write_log(int level, const char* format, ...)
{
    if (!running_.load())
    {
        return;
    }
    
    struct timeval now { 0, 0 };
    gettimeofday(&now, NULL); 
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    
    char s[16] = { 0 };

    switch (level)
    {
    case 0:
      strcpy(s, "[debug]:");
      break;
    case 1:
      strcpy(s, "[info]:");
      break;
    case 2:
      strcpy(s, "[warn]:");
      break;
    case 3:
      strcpy(s, "[erro]:");
      break;
    default:
      strcpy(s, "[info]:");
      break;
    }


    va_list valist;
    va_start(valist, format);

    std::array<char, 2024> buf{};
    int n = snprintf(buf.data(), 48, "%d-%02d-%02d %d:%d:%d.%06d %s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(buf.data() + n, buf.size() - n - 1, format, valist);
    buf[m + n] = '\n';
    buf[m + n + 1] = '\0';

    std::string log_str(buf.data());
    LogItem item{ std::move(log_str), my_tm };

    
    
    m_log_queue->push(std::move(item));
    va_end(valist);

}

void Log::write_item_to_file(const LogItem& tmp)
{
    const std::string day = format_day(tmp.tm);
    
    if (day !=m_current_day || m_lines_in_current_file >= m_splite_lines)
    {
        if (m_fp)
        {
            fflush(m_fp);
            fclose(m_fp);
        }

        if (day!=m_current_day)
        {
            m_current_day = day;
            m_file_index = 0;
            
        }
        else
        {
            ++m_file_index;
        }
        m_lines_in_current_file = 0;
        m_fp = fopen(make_log_path(tmp.tm, m_file_index).c_str(), "a");
        if (!m_fp)
        {
            return;
        }

    }

    fputs(tmp.line.c_str(), m_fp);
    ++m_lines_in_current_file;
}

std::string Log::format_day(const std::tm& my_tm)
{
    char day[32]{};
    std::strftime(day, sizeof(day), "%Y_%m_%d", &my_tm);
    return day;
}

std::filesystem::path Log::make_log_path(const std::tm& my_tm, long long index) const
{
    std::filesystem::path file = format_day(my_tm) + "_" + m_log_name;
    if (index > 0)
    {
        file += "." + std::to_string(index);
    }

    return m_log_root / file;
}