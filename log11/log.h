#ifndef _LOG_H
#define _LOG_H


#include"../include.h"
#include"message_queue.hpp"

#include <iostream>
#include <queue>
#include <vector>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include<sys/time.h>

class log
{
public:
    static log* get(){
        static log instance;
        return &instance;
    }
    bool init(const char* filename,int maxLines=500000,int queueSize=20,int logBufSize=1024,int threadNum=3);
    bool write_log(int level,const char* format,...);
    void flush(){
        pthread_mutex_lock(&m_mutex);
        fflush(m_fd);
        pthread_mutex_unlock(&m_mutex);        
    }
    virtual ~log();
private:
    log();
private:


    static void* worker(void* arg){
        log* mythis = (log*)arg;
        mythis->async_write_logfile();
        //get()->async_write_logfile();
        return NULL;
    }
    void async_write_logfile();
private:
    msg_queue<std::string>* m_log_queue;
    int m_queue_size;
    int m_maxlog_lines;
    size_t m_curlog_count;

    pthread_t* m_threads;
    int m_thread_size;

    char* m_log_buf;
    int m_log_buf_size;

    char m_dirname[256];
    char m_logname[256];
    int m_today;
    FILE* m_fd;

    int m_id;    //日志超出最大行数之后，新建日志文件尾部的编�?
private:
    pthread_mutex_t m_mutex;

};

#define LOG_DEBUG(format,...) log::get()->write_log(0,format,##__VA_ARGS__)
#define LOG_INFO(format,...) log::get()->write_log(1,format,##__VA_ARGS__)
#define LOG_WARNING(format,...) log::get()->write_log(2,format,##__VA_ARGS__)
#define LOG_ERROR(format,...) log::get()->write_log(3,format,##__VA_ARGS__)

#endif