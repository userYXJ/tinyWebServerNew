#ifndef _CONN_POOL_H
#define _CONN_POOL_H

#include"../include.h"

#include <iostream>
#include <list>
#include <pthread.h>
#include <semaphore.h>
#include <mysql/mysql.h>

class conn_pool
{
public:
    static conn_pool* get(){
        static conn_pool pool;
        return &pool;
    }
    void init(const char* url,const char* user,const char* passwd,const char* dbname,const int port,const int maxconn);
    MYSQL* getConnection();
    bool releaseConnection(MYSQL*);
    void destroyPool();
    ~conn_pool();
private:
    conn_pool(){};
    conn_pool(conn_pool&)=delete;
    void addConnection();

private:
    int m_maxconn;
    int m_curconn;
    int m_freeconn;
private:

    std::list<MYSQL*> m_conlist;
    sem_t m_sem;
    pthread_mutex_t m_mutex;
private:
    std::string m_url;
    std::string m_user;
    std::string m_passwd;
    std::string m_dbname;
    int m_port;
};


class connectionRAII
{
public:
    connectionRAII(MYSQL** con,conn_pool* pool);
    ~connectionRAII();
private:
    MYSQL* conRAII;
    conn_pool* poolRAII;
};
#endif