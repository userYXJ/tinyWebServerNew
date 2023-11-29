#ifndef _TIMER_H_
#define _TIMER_H_

#include"../log/log.h"

#include <iostream>
#include <list>
#include <arpa/inet.h>
class util_timer;
class client_data;
using funcptr=void(*)(client_data*);

/* 将客户端套接字与定时器封装到一起 */
struct client_data
{
    int m_sockfd;
    sockaddr_in m_address;
    util_timer* m_timer;
};

class util_timer
{
public:
    util_timer():m_prev(NULL),m_next(NULL)
    {
        m_expire=0;
    }
public:
    time_t m_expire;
    client_data* user_data;
    funcptr m_cb_func;
    util_timer* m_prev;
    util_timer* m_next;
};

/* 采用双向链表维护定时器，便于定时器的*/
class sort_timer_list
{
public:
    static sort_timer_list* get(){
        static sort_timer_list instance;
        return &instance;
    }
    bool add_timer(util_timer* timer);
    void trigger();
    bool del_timer(util_timer* timer);
    bool adjust_timer(util_timer* timer);
    ~sort_timer_list();
private:
    bool add_timer(util_timer* timer,util_timer* next);
private:
    sort_timer_list():m_head(NULL),m_tail(NULL){}
    sort_timer_list(sort_timer_list&)=delete;
private:
    util_timer* m_head;
    util_timer* m_tail;
};


#endif