#ifndef _MESSAGE_QUEUE_
#define _MESSAGE_QUEUE_
#include"../include.h"

#include <iostream>
#include <queue>
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
using namespace std;

template<class T>
class msg_queue
{
public:
    msg_queue(int maxSize);
    ~msg_queue();
public:
    bool push(const T& log_str);
    bool pop(T& log_str,int timeout);
    bool pop(T& log_str);
private:
    T* m_queue;
    int m_cur_size;
    int m_max_size;
    int m_front;
    int m_back;
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond_producer;
    pthread_cond_t m_cond_consumer;
};

template<class T>
msg_queue<T>::msg_queue(int maxSize)
{
    pthread_mutex_init(&m_mutex,NULL);
    pthread_cond_init(&m_cond_producer,NULL);
    pthread_cond_init(&m_cond_consumer,NULL);
    m_max_size=maxSize;
    m_cur_size=0;
    m_front=-1;
    m_back=-1;

    m_queue=new T[m_max_size];
    assert(m_queue);
}

template<class T>
msg_queue<T>::~msg_queue()
{
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond_producer);
    pthread_cond_destroy(&m_cond_consumer);
    if(m_queue)
        delete[] m_queue;
    m_queue=nullptr;
}

template<class T>
bool msg_queue<T>::push(const T& log_str)
{
    pthread_mutex_lock(&m_mutex);
    while(m_cur_size >= m_max_size){
        if(0!=pthread_cond_wait(&m_cond_producer,&m_mutex)){
            pthread_mutex_unlock(&m_mutex);
            return false;
        }
    }
    m_back=(m_back+1)%m_max_size;
    m_queue[m_back]=log_str;
    m_cur_size++;
    pthread_cond_broadcast(&m_cond_consumer);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

template<class T>
bool msg_queue<T>::pop(T& log_str,int timeout)
{

    pthread_mutex_lock(&m_mutex);
    while(m_cur_size<=0){
        struct timeval now={0,0};
        gettimeofday(&now,NULL);
        struct timespec t={0,0};
        t.tv_sec=now.tv_sec+timeout;
        std::cout<<"pthread_cond_timedwait"<<std::endl;
        if(0!=pthread_cond_timedwait(&m_cond_consumer,&m_mutex,&t)){
            pthread_mutex_unlock(&m_mutex);
            cout<<"return false"<<endl;
            return false;
        }
    }
    m_front=(m_front+1)%m_max_size;
    log_str=m_queue[m_front];
    m_cur_size--;
    pthread_cond_broadcast(&m_cond_producer);
    pthread_mutex_unlock(&m_mutex);
    return true;

}

template<class T>
bool msg_queue<T>::pop(T& log_str)
{

    pthread_mutex_lock(&m_mutex);
    while(m_cur_size<=0){
        if(0!=pthread_cond_wait(&m_cond_consumer,&m_mutex)){
            pthread_mutex_unlock(&m_mutex);
            cout<<"return false"<<endl;
            return false;
        }
    }
    m_front=(m_front+1)%m_max_size;
    log_str=m_queue[m_front];
    m_cur_size--;
    pthread_cond_broadcast(&m_cond_producer);
    pthread_mutex_unlock(&m_mutex);
    return true;

}
#endif