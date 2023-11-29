
#include"http/http_conn.h"
#include"mysql_conn_pool/conn_pool.h"
#include"thread_pool/thread_pool.hpp"
#include"log/log.h"
#include"timer/timer.h"

#include<sys/epoll.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<string.h>
#include<signal.h>

using namespace std;

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5


static int epollfd=0;
static int pipefd[2]{0};    //pipefd[0]--readfd   pipefd[1]--writefd

extern int setnoblocking(int fd);
extern void addfd(int epollfd,int fd,bool one_shot);
extern void removefd(int epollfd,int fd);

void send_error(int fd,const char* info)
{
    send(fd,info,strlen(info),0);
    close(fd);
}


/*
    定时器回调函数，释放客户端请求，当已连接的客户端长时间未进行数据交换时，
    定时器内部进行回调。这里的时间为(3*TIMESLOT)
*/
void cb_func(client_data* data)
{
    assert(data);
    removefd(epollfd,data->m_sockfd);
    http_conn::m_cur_count--;
#ifdef OPEN_LOG
    LOG_INFO("release client request (%s)",inet_ntoa(data->m_address.sin_addr));
#endif
}

/*
    SIGALRM，SIGTERM，SIGINT信号触发时，由系统调用
*/
void sig_handler(int sig)
{
    /*
        为保证函数的可重入性，保留原来的errno
        可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
        因为send函数会改变errno的值
    */
    int save_errno=errno;
    int save_sig=sig;
    send(pipefd[1],(char*)&save_sig,1,0);
    errno=save_errno;
}

/*
    将sig信号和对应的信号处理函数 注册进系统中，当系统接收到sig信号时，
    自动调用注册的信号处理函数，这里是sig_handler函数。
    restart == false，是为了中断程序，允许用户执行特定的操作。作用：向主循环发送信号
    restart == true，SIGPIPE信号中断系统调用时，如write()函数，操作系统会重启系统调用write(),返回-1并设置errno=EINTR
*/
void addsig(int sig,void(*handler)(int),bool restart=true)
{
    struct sigaction sa;
    bzero(&sa,sizeof(sa));
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);    //确保进程能够接收所有信号，除了已经忽略的信号，如本程序中的sigpipe信号
    if(restart)
        sa.sa_flags |= SA_RESTART;
    int ret=sigaction(sig,&sa,0);
    assert(0==ret);
}

/*
    定时时间到达TIMESLOT，调整定时器，同时释放3*TIMESLOT秒内未进行数据交换的客户端资源。
    内部会调用资源清理回调函数 void cb_func(client_data* data)。
*/
void timer_handler()
{
    sort_timer_list::get()->trigger();
    alarm(TIMESLOT);
}

int main(int argc,const char* argv[])
{
    int port = 5005;
    string logname = "serverlog";
    if(argc == 2){
        port = atoi(argv[1]);
    }
    else if(argc == 3){
        port = atoi(argv[1]);
        logname = argv[2];
    }
    else if(argc > 3){
        cout<<"./server [port] [logname]"<<endl;
        return -1;
    }

    addsig(SIGPIPE,SIG_IGN);    //忽略SIGPIPE信号
    //日志
    bool ok=log::get()->init(logname.c_str(),800000,20,2000,1);
    assert(ok);
    //连接池
    conn_pool* connPool=conn_pool::get();
    assert(connPool);
    connPool->init("localhost","root","yang1xv1jie1..","tinyserver",3306,8);
    //线程池
    thread_pool<http_conn>* threadPool=thread_pool<http_conn>::get(connPool,8,10000);
    assert(threadPool);
    //请求数组
    http_conn* requests = new http_conn[MAX_FD];
    assert(requests);
    requests->initmysql_result(connPool);
    //定时器->释放资源
    client_data* requests_timer=new client_data[MAX_FD];
    assert(requests_timer);
    
    //服务器地址
    sockaddr_in saddr;
    bzero(&saddr, sizeof(saddr));
    saddr.sin_addr.s_addr=htonl(INADDR_ANY);
    saddr.sin_port=htons(port);
    saddr.sin_family=AF_INET;
    
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);

    int flag=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));

    int ret=0;
    ret=bind(listenfd,(sockaddr*)&saddr,sizeof(sockaddr));
    assert(ret==0);
    ret=listen(listenfd,5);
    assert(ret==0);

    //内核事件
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd=epoll_create(5);
    assert(epollfd!=-1);
    http_conn::m_epollfd=epollfd;


    //设置listenfd  非阻塞、LT模式
    setnoblocking(listenfd);
    epoll_event event;
    event.events=EPOLLIN | EPOLLRDHUP;
    event.data.fd=listenfd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,listenfd,&event);

    //创建一对无名的、相互连接的套接字，用于向主循环发送定时器过期和程序终止信号
    ret=socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
    assert(0==ret);
    setnoblocking(pipefd[1]);           //设置写端为非阻塞,
    addfd(epollfd,pipefd[0],false);     //设置读端为ET模式，放入epoll中监听，直到TIMESLOT时间到达，触发了SIGALRM信号
    addsig(SIGALRM,sig_handler,false); 
    addsig(SIGTERM,sig_handler,false);  //kill pid
    addsig(SIGINT,sig_handler,false);   //CTRL + C 

    alarm(TIMESLOT);

    bool stop_server=false;
    bool timeout=false;

    while(!stop_server)
    {
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(num == -1 && errno != EINTR)
        {
        #ifdef OPEN_LOG
            LOG_ERROR("%s:%d epoll_wait failed!",__FUNCTION__,__LINE__);
        #endif
            break;
        }
        if(num == 0)
        {
        #ifdef OPEN_LOG
            LOG_ERROR("%s:%d epoll_wait timeout!",__FUNCTION__,__LINE__);
            break;
        #endif
        }

        for(int i=0;i<num;i++)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                struct sockaddr_in caddr;
                socklen_t len=sizeof(caddr);
                int clientfd;
                clientfd=accept(sockfd,(struct sockaddr*)&caddr,&len);
                if(clientfd == -1)
                {
                    LOG_ERROR("accept error:errno is:%d", errno);
                    continue;
                }
                if(http_conn::m_cur_count >= MAX_FD)
                {
                    send_error(sockfd,"internal server busy!");
                    LOG_ERROR("%s","internal server busy!");
                    continue;
                }
            #ifdef OPEN_LOG
                LOG_INFO("the client(%s) is connected",inet_ntoa(caddr.sin_addr));
            #endif
                requests[clientfd].init(clientfd,caddr);

                //添加定时器
                requests_timer[clientfd].m_sockfd=clientfd;
                requests_timer[clientfd].m_address=caddr;
                util_timer* timer=new util_timer;
                timer->m_expire=time(NULL)+3*TIMESLOT;
                timer->user_data=&requests_timer[clientfd];
                timer->m_cb_func=cb_func;
                requests_timer[clientfd].m_timer=timer;
                sort_timer_list::get()->add_timer(timer);

            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                util_timer* timer=requests_timer[sockfd].m_timer;
                assert(timer);
            #ifdef OPEN_LOG
                LOG_INFO("(%s,%d) close client(%s)",__FUNCTION__,__LINE__,inet_ntoa(requests[sockfd].get_addr()->sin_addr));
            #endif
                timer->m_cb_func(&requests_timer[sockfd]);
                sort_timer_list::get()->del_timer(timer);
            }
            else if(events[i].events&EPOLLIN && sockfd != pipefd[0])
            {    
                util_timer* timer=requests_timer[sockfd].m_timer;
                assert(timer);

                if(requests[sockfd].read_onec())
                {
                #ifdef OPEN_LOG
                    LOG_INFO("deal with client (%s)",inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                    assert(threadPool->add_request(requests+sockfd));
                    
                    timer->m_expire = time(NULL) + 3* TIMESLOT;
                    sort_timer_list::get()->adjust_timer(timer);
                #ifdef OPEN_LOG
                    LOG_INFO("adjust client(%s) timer",inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                }
                else
                {
                #ifdef OPEN_LOG
                    LOG_INFO("(%s,%d) close client(%s)",__FUNCTION__,__LINE__,inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                    timer->m_cb_func(&requests_timer[sockfd]);
                    sort_timer_list::get()->del_timer(timer);
                }
            }
            else if(events[i].events&EPOLLOUT)
            {
                util_timer* timer=requests_timer[sockfd].m_timer;
                assert(timer);
                if(requests[sockfd].write()){
                #ifdef OPEN_LOG
                    LOG_INFO("The request data(%s) was successfully sent to client(%s)",requests[sockfd].get_filename(),inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                    timer->m_expire = time(NULL) + 3* TIMESLOT;
                    sort_timer_list::get()->adjust_timer(timer);
                #ifdef OPEN_LOG
                    LOG_INFO("adjust (%s) timer",inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                }
                else{
                #ifdef OPEN_LOG
                    LOG_INFO("delete client(%s) timer",inet_ntoa(requests[sockfd].get_addr()->sin_addr));
                #endif
                    timer->m_cb_func(&requests_timer[sockfd]);
                    sort_timer_list::get()->del_timer(timer);
                }
            }
            else if(sockfd==pipefd[0]&&events[i].events&EPOLLIN)
            {
                char sig_buf[64]{0};
                //正常情况ret=1
                ret=recv(sockfd,sig_buf,sizeof(sig_buf),0);    
                if(ret==0){
                    continue;
                }
                else if(ret==-1){
                    LOG_ERROR("%s","receive timer signal failed!");
                    continue;
                }
                for(int i=0;i<ret;i++){
                    switch (sig_buf[i])
                    {
                    case SIGALRM:
                        timeout=true;
                        break;
                    case SIGTERM:
                        stop_server=true;
                        break;
                    case SIGINT:
                        stop_server=true;
                        break;
                    default:
                        LOG_ERROR("%s","receive timer signal error!");
                        break;
                    }
                }

            }
        }
        if(timeout){
            timer_handler();
            timeout=false;
        }
    }

    
    delete[] requests;
    delete[] requests_timer;
    close(listenfd);
    close(epollfd);
    close(pipefd[0]);
    close(pipefd[1]);
    cout<<endl<<"Exit normally after one second!"<<endl;
    sleep(1);
    return 0;
}
