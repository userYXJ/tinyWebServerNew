#ifndef _THREAD_POOL_HPP
#define _THREAD_POOL_HPP

#include"../include.h"
#include"../mysql_conn_pool/conn_pool.h"

#include <iostream>
#include <pthread.h>
#include <vector>
#include <list>
#include <semaphore.h>


template<class T>
class thread_pool
{
public:
    static thread_pool* get(conn_pool* connPool,size_t threadNum,size_t requestNum){
        static thread_pool pool(connPool,threadNum,requestNum);
        return &pool;
    }
    bool add_request(T* request);
    ~thread_pool();
private:
    thread_pool()=delete;
    thread_pool(conn_pool* connPool,size_t threadNum,size_t maxRequest);
    static void* worker(void* arg);
    void run();
private:
    conn_pool* m_conn_pool;
    std::vector<pthread_t> m_threads;
    size_t m_max_threads;
    size_t m_max_requests;
    std::list<T*> m_request_queue;

    pthread_mutex_t m_mutex;
    sem_t m_sem;
    pthread_cond_t m_cond_producer;
    pthread_cond_t m_cond_consumer;

    bool m_stop;

};

template<class T>
thread_pool<T>::thread_pool(conn_pool* connPool,size_t threadNum,size_t maxRequest)
{
    if(!connPool || threadNum <= 0 || maxRequest <= 0){
        throw std::exception();
    }
    m_conn_pool=connPool;
    m_max_threads=threadNum;
    m_max_requests=maxRequest;
    m_threads.resize(m_max_threads);
    
    pthread_mutex_init(&m_mutex,0);
    sem_init(&m_sem,0,0);
    pthread_cond_init(&m_cond_producer,0);
    pthread_cond_init(&m_cond_consumer,0);

    std::vector<pthread_t>::iterator it=m_threads.begin();
    for(;it != m_threads.end();++it){
        if(0 != pthread_create(&(*it),0,worker,this)){
            throw std::exception();
        }
        
        if(0 != pthread_detach(*it)){
            throw std::exception();
        }
    }

    m_stop = false;
}

template<class T>
thread_pool<T>::~thread_pool()
{
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond_consumer);
    pthread_cond_destroy(&m_cond_producer);
    sem_destroy(&m_sem);
}

template<class T>
bool thread_pool<T>::add_request(T* request)
{
    if(!request)
        return false;
    pthread_mutex_lock(&m_mutex);
    while(m_request_queue.size() > m_max_requests){
        if(0 != pthread_cond_wait(&m_cond_producer,&m_mutex)){
            return false;
        }
    }
    m_request_queue.push_back(request);
    sem_post(&m_sem);
    pthread_cond_broadcast(&m_cond_consumer);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

template<class T>
void* thread_pool<T>::worker(void* arg)
{
    thread_pool* threadpool=(thread_pool*)arg;
    threadpool->run();
    return NULL;
}

template<class T>
void thread_pool<T>::run()
{
    while(!m_stop){
        sem_wait(&m_sem);
        pthread_mutex_lock(&m_mutex);
        while(m_request_queue.empty()){
            if(0 != pthread_cond_wait(&m_cond_consumer,&m_mutex)){
                pthread_mutex_unlock(&m_mutex);
                return;
            }
        }
        T* request = m_request_queue.front();
        m_request_queue.pop_front();
        pthread_cond_broadcast(&m_cond_producer);
        pthread_mutex_unlock(&m_mutex);
        if(!request)
            continue;
        connectionRAII(&request->m_conn,m_conn_pool);
        request->process();

    }
}


#endif