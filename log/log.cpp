
#include"log.h"
using namespace std;

size_t log::m_curlog_count = 0;

log::log()
{
    pthread_mutex_init(&m_mutex,0);
    sem_init(&m_sem,0,0);
    m_log_queue=nullptr;
    bzero(m_dirname,sizeof(m_dirname));
    bzero(m_logname,sizeof(m_logname));
}

log::~log()
{
    pthread_mutex_destroy(&m_mutex);
    sem_destroy(&m_sem);
    if(m_log_queue)
        delete m_log_queue;
    m_log_queue=nullptr;
}

bool log::init(const char* filename,int maxLines,int queueSize,int logBufSize,int threadNum)
{
    if(strlen(filename) >= 256){
        cout<<"filename is to long"<<endl;
        return false;
    }
    if(maxLines<500000||queueSize<20||logBufSize<1024||threadNum<=0){
        maxLines=500000;
        queueSize=20;
        logBufSize=1024;
        threadNum=1;
    }
    m_maxlog_lines=maxLines;
    m_queue_size=queueSize;
    m_log_buf_size=logBufSize;

    m_id=1;

    m_log_queue=new msg_queue<std::string>;

    for(int i=0;i<threadNum;i++){
        pthread_t w_pid,p_pid;
        if((0!=pthread_create(&w_pid,NULL,worker,this)) || 
            (0!=pthread_create(&p_pid,NULL,producer,this))){
            throw std::exception();
        }
        if((0!=pthread_detach(w_pid)) || 
            0!=pthread_detach(p_pid)){
            throw std::exception();
        }
        m_producer_pid.push_back(p_pid);
        m_worker_pid.push_back(w_pid);
    }

    time_t t=time(NULL);
    struct tm* sys_tm=localtime(&t);
    struct tm my_tm=*sys_tm;
    
    const char* pos=strrchr(filename,'/');
    char log_full_name[512];                //全称
    if(pos==NULL){
        strcpy(m_logname,filename);
        snprintf(log_full_name,sizeof(log_full_name)-1,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,filename);
    }
    else{
        strcpy(m_logname,pos+1);
        strncpy(m_dirname,filename,pos-filename+1);
        m_dirname[pos-filename]='\0';
        snprintf(log_full_name,sizeof(log_full_name)-1,"%s%d_%02d_%02d_%s",m_dirname,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,filename);
    }
    m_today=my_tm.tm_mday;

    m_fd=fopen(log_full_name,"a");
    if(!m_fd){
        throw std::exception();
    }

    return true;
}

bool log::write_log(int level,const char* format,...)
{
    char type[16]{0};
    char loginfo[m_log_buf_size]{0};
    switch (level)
    {
    case 0:
        strcpy(type, "[debug]:");
        break;
    case 1:
        strcpy(type, "[info]:");
        break;
    case 2:
        strcpy(type, "[warn]:");
        break;
    case 3:
        strcpy(type, "[erro]:");
        break;
    default:
        strcpy(type, "[info]:");
        break;
    }

    int n = snprintf(loginfo,16,type);

    //将传入的format参数赋值给valst，便于格式化输出
    va_list valst;
    va_start(valst,format);
    int m = vsnprintf(loginfo + n,m_log_buf_size - 2,format,valst);
    va_end(valst);

    loginfo[n+m]='\n';
    string log_str{loginfo};

    pthread_mutex_lock(&m_mutex);
    m_loginfo.push(log_str);
    sem_post(&m_sem);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

void log::async_write_logfile()
{
    while(1){
        string log_str="";
        if(!m_log_queue->pop(log_str))
            continue;
        fputs(log_str.c_str(),m_fd);
        flush();
    }
}

void log::async_write_loginfo(){
    string loginfo;
    while(true){
        sem_wait(&m_sem);
        pthread_mutex_lock(&m_mutex);
        loginfo = m_loginfo.front();
        m_loginfo.pop();
        if(!write_loginfo_2Log_queue(loginfo))
            return;
        pthread_mutex_unlock(&m_mutex);

        m_log_queue->push(loginfo);
    }
}

bool log::write_loginfo_2Log_queue(string& loginfo){
    
    struct timeval now={0,0};
    gettimeofday(&now,NULL);
    time_t t=time(NULL);
    struct tm *sys_tm=localtime(&t);
    struct tm my_tm=*sys_tm;
    
    char logbuf[m_log_buf_size];
    snprintf(logbuf,m_log_buf_size,"%d-%02d-%02d:%02d-%02d-%02d.%06ld %s ",
            my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,my_tm.tm_hour,
            my_tm.tm_min,my_tm.tm_sec,now.tv_usec,loginfo.c_str());

    loginfo.assign(logbuf);

    m_curlog_count++;

    if(my_tm.tm_mday!=m_today || (m_curlog_count%m_maxlog_lines)==0){
        fflush(m_fd);
        fclose(m_fd);
        char new_log_file[530];
        char date[16]{0};
        sprintf(date,"%d_%02d_%02d",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday);
        if(my_tm.tm_mday!=m_today){
            m_today=my_tm.tm_mday;
            snprintf(new_log_file,sizeof(new_log_file),"%s%s_%s",m_dirname,date,m_logname);
        }
        if(m_curlog_count%m_maxlog_lines==0){
            snprintf(new_log_file,sizeof(new_log_file),"%s%s_%s.%d",m_dirname,date,m_logname,m_id++);
        }
        m_curlog_count=0;
        m_fd=fopen(new_log_file,"a");
        if(!m_fd){
            printf("srcfile[%s] func[%s] open file %s failed\n",__FILE__,__func__,new_log_file);
            return false;
        }
    }

    return true;

}