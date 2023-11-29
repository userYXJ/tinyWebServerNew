#ifndef _MESSAGE_QUEUE_
#define _MESSAGE_QUEUE_
#include"../include.h"

#include <iostream>
#include <queue>
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>

using namespace std;

template<class T>
class msg_queue
{
public:
    msg_queue();
    ~msg_queue();
public:
    bool push(const T& log_str);
    bool pop(T& log_str,int timeout);
    bool pop(T& log_str);
private:
    std::queue<T> m_queue;
    inline size_t get_queuesize(){
        return m_queue.size();
    }
private:
    pthread_mutex_t m_mutex;
    sem_t m_sem;
    
};

// template<class T>
// pthread_mutex_t msg_queue<T>::m_mutex = PTHREAD_MUTEX_INITIALIZER;


template<class T>
msg_queue<T>::msg_queue()
{
    pthread_mutex_init(&m_mutex,0);
    sem_init(&m_sem,0,0);
}

template<class T>
msg_queue<T>::~msg_queue()
{
    pthread_mutex_destroy(&m_mutex);
    sem_destroy(&m_sem);
}

template<class T>
bool msg_queue<T>::push(const T& log_str)
{
    pthread_mutex_lock(&m_mutex);
    m_queue.push(log_str);
    sem_post(&m_sem);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

template<class T>
bool msg_queue<T>::pop(T& log_str,int timeout)
{

    sem_wait(&m_sem);
    pthread_mutex_lock(&m_mutex);

    struct timeval now={0,0};
    gettimeofday(&now,NULL);
    struct timespec t={0,0};
    t.tv_sec=now.tv_sec+timeout;    
    if(0!=sem_timedwait(&m_sem,&t)){
        pthread_mutex_unlock(&m_mutex);
        printf("file[%s] func[%s] sem_timedwait failed\n",__FILE__,__func__);
        return false;
    }

    log_str = m_queue.front();
    m_queue.pop();

    pthread_mutex_unlock(&m_mutex);
    return true;
}

template<class T>
bool msg_queue<T>::pop(T& log_str)
{

    sem_wait(&m_sem);
    pthread_mutex_lock(&m_mutex);

    log_str=m_queue.front();
    m_queue.pop();

    pthread_mutex_unlock(&m_mutex);
    return true;
}
#endif