#ifndef _HTTP_CONN_H
#define _HTTP_CONN_H

#include"../include.h"
#include"../mysql_conn_pool/conn_pool.h"
#include"../log/log.h"

#include<iostream>
#include<mysql/mysql.h>
#include<arpa/inet.h>
#include<assert.h>
#include<map>
#include<unordered_map>
#include<unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<dirent.h>
class http_conn
{
public:

    enum METHOD
    {
        GET=0,
        POST
    };

    enum CHECK_STATE
    {
        PARSE_REQUEST_LINE=0,
        PARSE_REQUEST_HEADER,
        PARSE_REQUEST_CONTENT
    };

    enum HTTP_CODE
    {
        NO_REQUEST=0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        DIR_REQUEST,
        INTERNAL_ERROR
    };

    enum LINE_STATUS
    {
        LINE_OK=0,
        LINE_BAD,
        LINE_NO
    };

public:
    http_conn();
    ~http_conn();
    void initmysql_result(conn_pool* connPool);
    void init(int sockfd,const sockaddr_in& address);
    void init();
    bool read_onec();
    void process();
    bool write();
    const char* get_filename(){
        return m_real_file.c_str();
    }
public:
    sockaddr_in* get_addr(){
        return &m_address;
    }
private:
    HTTP_CODE process_read();
    LINE_STATUS parse_line();
    bool process_write(HTTP_CODE);
    inline char* get_line(){return m_read_buf+m_line_start;}
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_request_header(char* text);
    HTTP_CODE parse_request_content(char* text);
    HTTP_CODE do_request();
    void unmap();

private:
    bool add_response(const char* format,...);
    bool add_status_line(int status_code,const char* description);
    bool add_header(int content_length);
    bool add_content_length(int content_length);
    bool add_file_type(const char* filename);
    bool add_connection();
    bool add_blank_line();
    bool add_content(const char* content);

    bool send_dentry();
public:
    static const int READ_BUF_SIZE = 1024;
    static const int WRITE_BUF_SIZE = 2048;
public:
    static int m_epollfd;
    static int m_cur_count;
    MYSQL* m_conn;
private:
    int m_sockfd;
    sockaddr_in m_address;
    std::string m_dirname;
    std::string m_filename;

    char m_read_buf[READ_BUF_SIZE];
    int m_read_idx;
    int m_line_start;
    int m_checked_idx;
    char m_write_buf[WRITE_BUF_SIZE];
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    char m_url[256];
    char m_version[10];
    
    char* m_host;
    bool m_linger;
    int m_content_length;
    std::string m_user_passwd;
    std::string m_real_file;

    struct stat m_file_stat;
    char* m_file_address;            //请求文件的内存映射地址
    struct iovec m_iv[2];
    int m_iv_count;
    size_t m_bytes_to_send;

    pthread_mutex_t m_mutex;

    char m_dir_buf[2048];
    int m_dir_idx;

};


#endif