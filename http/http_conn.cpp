#include"http_conn.h"
#include"../file_type.h"
using namespace std;

#define CLIENT_ET
#define LISTENT_LT

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


int http_conn::m_epollfd=-1;
int http_conn::m_cur_count=0;
unordered_map<string,string> users;

const char* src_dir="/root/VSCode/tinyWebServer/tinyWebServer/";
void addfd(int,int,bool);
void modfd(int,int,int);
void removefd(int,int);
extern const char* get_file_type(const char* file);


http_conn::http_conn()
{
    assert(0 == chdir(src_dir));
}

http_conn::~http_conn()
{
    pthread_mutex_destroy(&m_mutex);
}

void http_conn::initmysql_result(conn_pool* connPool)
{
    MYSQL* mysql=NULL;
    connectionRAII connRAII(&mysql,connPool);

    if(0 != mysql_query(mysql,"SELECT username,passwd FROM user")){
        LOG_ERROR("%s:%d %s",__FUNCTION__,__LINE__,mysql_error(mysql));
        return;
    }
    MYSQL_RES * result=mysql_store_result(mysql);
    MYSQL_ROW row;
    while((row=mysql_fetch_row(result))){
        //cout<<"user = "<<row[0]<<" password = "<<row[1]<<endl;
        users.insert(make_pair(row[0],row[1]));
    }

}

void http_conn::init(int sockfd,const sockaddr_in& address)
{
    m_sockfd=sockfd;
    m_address=address;
    m_cur_count++;
    pthread_mutex_init(&m_mutex,NULL);
    addfd(m_epollfd,m_sockfd,true);
    init();
}

void http_conn::init()
{
    
    bzero(m_read_buf,READ_BUF_SIZE);
    bzero(m_write_buf,WRITE_BUF_SIZE);
    m_read_idx=0;
    m_line_start=0;
    m_checked_idx=0;
    m_write_idx=0;

    m_check_state=PARSE_REQUEST_LINE;
    m_method=GET;

    m_host=NULL;
    m_linger=false;
    m_content_length=0;

    m_file_address=NULL;
    m_iv_count=0;
    m_bytes_to_send=0;
}

bool http_conn::read_onec()
{
    if(m_read_idx >= READ_BUF_SIZE)
        return false;
    int readbytes=0;
    while(true){
        readbytes=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUF_SIZE-m_read_idx,0);
        if(readbytes == -1){
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                break;
            }
            return false;
        }
        if(readbytes==0){
            return false;
        }
        m_read_idx+=readbytes;
    }
    return true;
}

/*
    在线程池中调用，同时初始化了成员变量m_conn用于与数据库进行连接
*/
void http_conn::process()
{
    HTTP_CODE ret = process_read();
    if(ret == NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }
    if(!process_write(ret)){
        removefd(m_epollfd,m_sockfd);
        m_cur_count--;
        m_sockfd = -1;
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}

http_conn::HTTP_CODE http_conn::process_read()
{
    HTTP_CODE http_status=NO_REQUEST;
    LINE_STATUS line_status=LINE_OK;
    char* text;
    while((m_check_state == PARSE_REQUEST_CONTENT && line_status == LINE_OK) || 
        (line_status=parse_line())==LINE_OK){

        text=get_line();    //从m_read_buf中拿出一行数据

    #ifdef OPEN_LOG
        LOG_INFO("%s", text);
    #endif
        m_line_start=m_checked_idx; //更新下次读取的首位置

        //对拿到的数据进行解析处理
        switch (m_check_state)
        {
        case PARSE_REQUEST_LINE:
            http_status=parse_request_line(text);
            if(http_status == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        case PARSE_REQUEST_HEADER:
            http_status = parse_request_header(text);
            if(http_status == BAD_REQUEST)
                return BAD_REQUEST;
            else if(http_status == GET_REQUEST)
                return do_request();
            break;
        case PARSE_REQUEST_CONTENT:
            http_status = parse_request_content(text);
            if(http_status == GET_REQUEST)
                return do_request();
            line_status = LINE_NO;
            break;
        default:
            return BAD_REQUEST;
        }
    }
    return NO_REQUEST;
}

/* 从m_read_buf中解析出一行与 “\\r\\n” 结尾的一行数据 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        tmp=m_read_buf[m_checked_idx];
        if(tmp=='\r'){
            if((m_checked_idx+1)==m_read_idx){
                return LINE_NO;
            }
            else if(m_read_buf[m_checked_idx+1]=='\n'){
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tmp=='\n'&&m_checked_idx>0){
            if(m_read_buf[m_checked_idx -1]=='\r'){
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_NO;
}

/* 解析请求行（GET /resource HTTP/1.1) 拿到 请求类型、资源文件、版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    char* tmp = strpbrk(text," \t"); // 返回第一次出现字符' '和字符 '\t' 出现的位置
    char* url;
    char* version;
    if(!tmp)
        return BAD_REQUEST;
    *tmp++ = '\0';
    char* method = text;
    if(0 == strcasecmp(method,"GET"))
        m_method = GET;
    else if(0 == strcasecmp(method,"POST"))
        m_method = POST;
    else
        return BAD_REQUEST;
    // tmp ->  "/resource HTTP/1.1"
    tmp += strspn(tmp," \t");
    url = tmp;
    version = strpbrk(tmp," \t");
    if(!version)
        return BAD_REQUEST;
    *version++ = '\0';
    // tmp ->  "/resource'\0'HTTP/1.1"
    version += strspn(version," \t");
    strcpy(m_version,version);        //获取版本号
    if(0 != strcasecmp(version,"HTTP/1.1"))
        return BAD_REQUEST;

    if(0 == strncasecmp(url,"http://",7)){
        url += 7;
        url = strchr(url,'/');
    }
    else if(0 == strncasecmp(url,"https://",8)){
        url += 8;
        url = strchr(url,'/');
    }
    if(!url || url[0] != '/'){
        return BAD_REQUEST;
    }
    
    if(0 == strcmp(url,"/"))
        strcpy(m_url,"/htmlFile/judge.html");
    else
        strcpy(m_url,url);
    m_check_state = PARSE_REQUEST_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_header(char* text)
{
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = PARSE_REQUEST_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text,"Host:",4) == 0){
        text += 4;
        text += strspn(text," \t");
        m_host = text;
    }
    else if(strncasecmp(text,"Content-Length:",15) == 0){
        text += 15;
        text += strspn(text," \t");
        m_content_length = atoi(text);
    }
    else if(strncasecmp(text,"Connection:",11) == 0){
        text += 11;
        text +=strspn(text," \t");
        if(strcasecmp(text,"Keep-Alive") == 0){
            m_linger = true;
        }
    }
    
    return NO_REQUEST;

}

http_conn::HTTP_CODE http_conn::parse_request_content(char* text)
{
    if(m_read_idx >= m_content_length + m_checked_idx){
        text[m_content_length] = '\0';
        m_user_passwd = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    // 关闭直接通过路径直接访问资源的权限
    // if(m_user_passwd.empty())
    //     return FORBIDDEN_REQUEST;
    m_real_file.assign(src_dir);
    const char* tmp=strrchr(m_url,'/');
    //cout<<"m_url = "<<m_url<<endl;
    if((m_method == POST) && ((*(tmp+1) == '2') || (*(tmp+1) == '3'))){
        string name="";
        string passwd="";
        //user=aaaa&password=yang1xv1jie1..
        int pos=m_user_passwd.find('&');
        name.assign(m_user_passwd,5,pos-5);
        passwd.assign(m_user_passwd,pos+10,m_user_passwd.size());

        //登录判断
        if(*(tmp+1) == '2'){
            if(users.find(name) != users.end() && users[name] == passwd)
                strcpy(m_url,"/htmlFile/welcome.html");     
            else
                strcpy(m_url,"/htmlFile/loginError.html");
        }
        else if(*(tmp+1) == '3'){
            if(users.find(name) == users.end()){
                string sql="INSERT INTO user(username,passwd) VALUES('" + name + "','" + passwd + "');";
                pthread_mutex_lock(&m_mutex);
                int ret = mysql_query(m_conn,sql.c_str());
                users[name]=passwd;
                pthread_mutex_unlock(&m_mutex);
                if(0 != ret){
                    LOG_ERROR("%s() : %d %s",__FUNCTION__,__LINE__,mysql_error(m_conn));
                    strcpy(m_url,"/htmlFile/registerError.html");
                }
                else
                    strcpy(m_url,"/htmlFile/login.html");
            }
            else 
                strcpy(m_url,"/htmlFile/registerError.html");
        }
    }
    if(*(tmp+1) == '0')                           //注册
        strcpy(m_url,"/htmlFile/register.html");       
    else if(*(tmp+1) == '1')                      //登录
        strcpy(m_url,"/htmlFile/login.html");
    else if(*(tmp+1) == '4')                      //整个source目录
        strcpy(m_url,"/htmlFile/directoryResource.html");

    m_real_file.append((m_url+1));          //绝对路径

    // int len1=strlen(m_url) + 8;
    // int len2=m_real_file.size() + 14;
    // int maxlen=(len1 > len2 ? len1:len2);

    // for(int i=0;i<maxlen;i++)
    //     cout<<"-";
    // cout<<endl;
    // cout<<"m_url = "<<m_url<<endl;
    // cout<<"m_real_file = "<<m_real_file<<endl;
    // for(int i=0;i<maxlen;i++)
    //     cout<<'-';
    // cout<<endl;

    if(0 != stat(m_real_file.c_str(),&m_file_stat))
        return NO_RESOURCE;
    // 设置只允许通过路径访问的资源，函数最前面有控制该功能的开关
    if((!(m_file_stat.st_mode & S_IROTH)) 
        || ((strncasecmp(m_url+1,"resource",8) != 0)
        && (strncasecmp(m_url+1,"htmlFile",8) != 0) 
        && (strncasecmp(m_url+1,"favicon.ico",11) != 0)))
        return FORBIDDEN_REQUEST;
    if(S_ISDIR(m_file_stat.st_mode))
        return DIR_REQUEST;
    // if(0 == m_file_stat.st_size)                 //请求文件大小等于0，返回空页面
    //     return FILE_REQUEST;
    int fd = open(m_real_file.c_str(),O_RDONLY);
    if(-1 == fd){
        LOG_ERROR("open file(%s) failed",m_real_file.c_str());
        return INTERNAL_ERROR;
    }
    m_file_address = (char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = NULL;
    }
}

/* 
    将要发送的数据准备好，存放到 struct iovec m_iv[2] 中,
    当fd可读时通过主线程调用成员函数write响应数据给客户端 
*/
bool http_conn::process_write(http_conn::HTTP_CODE status_code)
{
    switch(status_code)
    {
        case BAD_REQUEST:
        {
            //m_real_file = "failed status code = 400";
            if(!(add_status_line(400,error_400_title)&&
                    add_header(strlen(error_400_form))&&
                            add_content(error_400_form)))
                return false;
            break;
        }
        case NO_RESOURCE:
        {
            //m_real_file = "failed status code = 404";
            if(!(add_status_line(404,error_404_title)&&
                add_header(strlen(error_404_form))&&
                        add_content(error_404_form)))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            //m_real_file = "failed status code = 403";
            if(!(add_status_line(403,error_403_title)&&
                add_header(strlen(error_403_form))&&
                        add_content(error_403_form)))
                return false;
            break;         
        }
        case INTERNAL_ERROR:
        {
            //m_real_file = "failed status code = 500";
            if(!(add_status_line(500,error_500_title)&&
                add_header(strlen(error_500_form))&&
                        add_content(error_500_form)))
                return false;
            break;         
        }
        case FILE_REQUEST:
        {
            if(!(add_status_line(200,ok_200_title)&&add_file_type(m_url)))
                return false;
            if(m_file_stat.st_size != 0){
                if(!add_header(m_file_stat.st_size))
                    return false;
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                m_bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }

            const char* empty="<!DOCTYPE html><html><head><title>empty</title></head><body></body></html>";
            if(!(add_header(strlen(empty))&&add_content(empty)))
                    return false;
            break;
        }
        case DIR_REQUEST:
        {
            if(!(add_status_line(200,ok_200_title)&&add_file_type(".html")&&add_header(-1)))
                return false;

            if(m_file_stat.st_size != 0){
                if(!(add_header(m_file_stat.st_size)&&send_dentry()))
                    return false;
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_dir_buf;
                m_iv[1].iov_len = m_dir_idx;
                m_iv_count = 2;
                m_bytes_to_send = m_write_idx + m_dir_idx;
                return true;
            }

            const char* empty="<!DOCTYPE html><html><head><title>empty</title></head><body></body></html>";
            if(!(add_header(strlen(empty))&&add_content(empty)))
                    return false;
            break;
        }
        default:
            break;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::add_response(const char* format,...)
{
    if(m_write_idx >= WRITE_BUF_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list,format);

    int len = vsnprintf(m_write_buf + m_write_idx,WRITE_BUF_SIZE - 1,format,arg_list);
    if(len >= (WRITE_BUF_SIZE - m_write_idx -1)){
        va_end(arg_list);
        return false;
    }
#ifdef OPEN_LOG
    LOG_INFO("response:%s",m_write_buf + m_write_idx);
#endif
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status_code,const char* description)
{
    return add_response("HTTP/1.1 %d %s\r\n",status_code,description);
}

bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length:%d\r\n",content_length);
}

bool http_conn::add_connection()
{
    return add_response("Connection:%s\r\n",m_linger?"Keep-alive":"close");
}

bool http_conn::add_file_type(const char* filename)
{
    return add_response("Content-Type:%s; \r\n",get_file_type(filename));
}

bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}

bool http_conn::add_header(int content_length)
{
    return add_content_length(content_length)&&add_connection()&&add_blank_line();
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s",content);
}

/* 直接用数组拼接html文件，发送目录项 */
bool http_conn::send_dentry()
{
    m_dir_idx = sprintf(m_dir_buf,"<!DOCTYPE html><html><head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/><title>所在目录：%s</title></head>",m_url+1);
    m_dir_idx += sprintf(m_dir_buf+m_dir_idx,"<body><h1>当前目录：%s</h1><table>",m_url+1);

    struct dirent** ptr;
    int nums = scandir(m_real_file.c_str(),&ptr,NULL,alphasort);
    string path;
    //cout<<"m_real_file = "<<m_real_file<<endl;
    for(int i=0;i<nums;i++){
        const char* filename = ptr[i]->d_name;
        path = (m_url+1) +""s+ filename;
        //cout<<"path = "<<path<<endl;
        struct stat st;
        if(0 != stat(path.c_str(),&st)){
            LOG_ERROR("%s:%d (%s) %s",__FUNCTION__,__LINE__,path.c_str(),strerror(errno));
            return false;
        }

        if(S_ISREG(st.st_mode))
            m_dir_idx += sprintf(m_dir_buf+m_dir_idx,"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",filename,filename,static_cast<long>(st.st_size));
        if(S_ISDIR(st.st_mode))
            m_dir_idx += sprintf(m_dir_buf+m_dir_idx,"<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",filename,filename,static_cast<long>(st.st_size));
    }
    m_dir_idx += sprintf(m_dir_buf+m_dir_idx,"</table></body></html>");
    return true;
}

/* 
    内部调用的是writev发送 iovec 中的数据 
    返回值为true表示长连接，不对客户端资源进行回收，false回收资源
*/
bool http_conn::write()
{
    if(m_bytes_to_send == 0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        unmap();
        return true;
    }
    size_t haved_to_send=0;
    int ret=0;
    while(m_bytes_to_send > 0){
        ret = writev(m_sockfd,m_iv,m_iv_count);
        if(ret == -1){
            if(errno == EAGAIN){
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }       
        haved_to_send += ret;
        m_bytes_to_send -= ret;
        if(haved_to_send >= m_iv[0].iov_len){
            m_iv[0].iov_len=0;
            m_iv[1].iov_base=m_file_address + haved_to_send - m_write_idx;
            m_iv[1].iov_len=m_bytes_to_send;
        }
        else{
            m_iv[0].iov_base=m_write_buf + haved_to_send;
            m_iv[0].iov_len -= haved_to_send;
        }
    }
    unmap();
    modfd(m_epollfd,m_sockfd,EPOLLIN);

    //长连接，不进行资源的释放
    if(m_linger){
        init();                 //对非连接资源重新初始化
        return true;            
    } 

    return false;               //释放资源
}

int setnoblocking(int fd)
{
    //fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

/*
    EPOLLONESHOT
        标志的作用是保证每个文件描述符（FD）只被epoll监听器触发一次
        当某个FD上的事件被epoll_wait函数返回后，epoll会自动将该FD的EPOLLONESHOT标志设置为已禁用
        即使该FD上的事件再次发生，也不会再次触发监听器。直到用户重新将该FD加入到epoll监听队列中，
        才能再次触发监听
*/
void addfd(int epollfd,int fd,bool one_shot)
{
    setnoblocking(fd);
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLRDHUP|EPOLLET;
    // cout<<"epollfd = "<<epollfd <<" fd = "<<fd<<" one_shot = "<<one_shot<<endl;
    if(one_shot)
        event.events |= EPOLLONESHOT;
    assert(0 == epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event));

}

void modfd(int epollfd,int fd,int ev)
{
    struct epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}