#include"timer.h"

sort_timer_list::~sort_timer_list()
{
    util_timer* tmp = m_head;
    while(tmp){
        m_head = m_head->m_next;
        delete tmp;
        tmp = m_head;
    }
}

bool sort_timer_list::add_timer(util_timer* timer)
{
    if(!timer)
        return false;
    if(!m_head){
        m_tail=m_head=timer;
        return true;
    }
    if(timer->m_expire <= m_head->m_expire){
        timer->m_next=m_head;
        timer->m_prev=NULL;
        m_head->m_prev=timer;
        m_head=timer;
        return true;
    }
    add_timer(timer,m_head);
    return true;
}

/* 链表的构造，是对m_expire成员变量进行升序构造的 */
bool sort_timer_list::add_timer(util_timer* timer,util_timer* next)
{
    util_timer* tmp=next->m_next;
    while(tmp){
        if(timer->m_expire <= tmp->m_expire){
            timer->m_next=tmp;
            timer->m_prev=tmp->m_prev;
            tmp->m_prev->m_next=timer;
            tmp->m_prev=timer;
            return true;
        }
        tmp=tmp->m_next;
    }
    timer->m_prev=m_tail;
    timer->m_next=NULL;
    m_tail->m_next=timer;
    m_tail=timer;
    return true;
}

/* 释放长时间未进行数据交换的连接 */
void sort_timer_list::trigger()
{
    
    if(!m_head){
        return;
    }
#ifdef OPEN_LOG
    LOG_INFO("timer triger");
#endif
    time_t curtime=time(NULL);
    util_timer* tmp=m_head;
    while(tmp)
    {
        if(curtime <= tmp->m_expire){
            return;
        }
        tmp->m_cb_func(tmp->user_data);
        m_head=tmp->m_next;
        if(m_head)
            m_head->m_prev=NULL;
        delete tmp;
        tmp=m_head;
    }
}

bool sort_timer_list::del_timer(util_timer* timer)
{
    if(!timer)
        return false;
    if(m_head==timer && m_tail==timer){
        m_head=m_tail=NULL;
        goto done;
    }
    else if(m_head == timer){
        m_head=m_head->m_next;
        m_head->m_prev=NULL;
        goto done;
    }
    else if(timer==m_tail){
        m_tail=m_tail->m_prev;
        m_tail->m_next=NULL;
        goto done;
    }
    
    timer->m_prev->m_next=timer->m_next;
    timer->m_next->m_prev=timer->m_prev;
done:
    delete timer;
    timer=NULL;
    return true;
}

/* 若timer需要调整，则是将该定时器从链表中脱离出去，然后在重新插入到链表中 */
bool sort_timer_list::adjust_timer(util_timer* timer)
{
    if(!timer||!m_head)
        return false;

    /* 位于链表尾端和比下一个定时器的过期时间早 的定时器 不需要调整 */
    if(timer==m_tail || timer->m_expire <= timer->m_next->m_expire){
        return true;
    }

    
    if(timer==m_head){
        m_head=m_head->m_next;
        m_head->m_prev=NULL;
        timer->m_next=NULL;
        goto success;
    }
    timer->m_prev->m_next=timer->m_next;
    timer->m_next->m_prev=timer->m_prev;
    timer->m_prev=NULL;
    timer->m_next=NULL;
success:
    add_timer(timer);
    return true;
}
