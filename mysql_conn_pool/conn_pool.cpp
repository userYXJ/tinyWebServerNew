#include"conn_pool.h"
using namespace std;
void conn_pool::init(const char* url,const char* user,const char* passwd,const char* dbname,const int port,const int maxconn)
{
    m_url=url;
    m_user=user;
    m_passwd=passwd;
    m_dbname=dbname;
    m_port=port;
    
    m_maxconn=maxconn;
    m_curconn=0;

    pthread_mutex_init(&m_mutex,NULL);

    for(int i=0;i<maxconn;i++){
        addConnection();
    }
    
    sem_init(&m_sem,0,m_freeconn);

}

void conn_pool::addConnection()
{
    MYSQL* mysql=NULL;
    mysql = mysql_init(NULL);
    if(!mysql){
        cout<<__FUNCTION__<<":"<<__LINE__<<" "<<mysql_error(mysql)<<endl;
        return;
    }

    if(!mysql_real_connect(mysql,m_url.c_str(),m_user.c_str(),m_passwd.c_str(),m_dbname.c_str(),m_port,NULL,0)){
        cout<<__FUNCTION__<<":"<<__LINE__<<" "<<mysql_error(mysql)<<endl;
        mysql_close(mysql);
        return;
    }
    m_conlist.push_back(mysql);
    m_freeconn++;
    
}

MYSQL* conn_pool::getConnection()
{
    MYSQL* con=NULL;
    sem_wait(&m_sem);
    pthread_mutex_lock(&m_mutex);
    con=m_conlist.front();
    m_conlist.pop_front();
    m_freeconn--;
    m_curconn++;
    pthread_mutex_unlock(&m_mutex);
    return con;
}

bool conn_pool::releaseConnection(MYSQL* con)
{
    if(!con)
        return false;

    pthread_mutex_lock(&m_mutex);
    m_conlist.push_back(con);
    m_freeconn++;
    m_curconn--;
    sem_post(&m_sem);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

void conn_pool::destroyPool()
{
    pthread_mutex_lock(&m_mutex);
    if(m_conlist.size()>0){
        list<MYSQL*>::iterator it=m_conlist.begin();
        for(;it!=m_conlist.end();++it){
            mysql_close(*it);
        }
        m_curconn=0;
        m_freeconn=0;
        m_conlist.clear();
    }
    pthread_mutex_unlock(&m_mutex);
}

conn_pool::~conn_pool()
{
    destroyPool();
    pthread_mutex_destroy(&m_mutex);
    sem_destroy(&m_sem);
}

connectionRAII::connectionRAII(MYSQL** con,conn_pool* pool)
{
    *con=pool->getConnection();
    conRAII=*con;
    poolRAII=pool;
}
connectionRAII::~connectionRAII()
{
    poolRAII->releaseConnection(conRAII);
}
