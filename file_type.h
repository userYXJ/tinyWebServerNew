#ifndef _FILE_TYPE_H
#define _FILE_TYPE_H
#include<string.h>


const char* get_file_type(const char* file)
{
    
    const char* dot=strrchr(file,'.');
    if(dot == NULL)
        return "text/plain; charset=utf-8";
    if(strcmp(dot,".html")==0 || strcmp(dot,".htm")==0)
        return "text/html; charset=utf-8";
    if(strcmp(dot,".jpg")==0 || strcmp(dot,".jpeg")==0)
        return "image/jpeg";
    if(strcmp(dot,".gif")==0)
        return "image/gif";
    if(strcmp(dot,".png")==0)
        return "image/png";
    if(strcmp(dot,".css")==0)
        return "text/css";
    if(strcmp(dot,".au")==0)
        return "audio/basic";
    if(strcmp(dot,".wav")==0)
        return "audio/wav";
    if(strcmp(dot,".avi")==0)
        return "video/x-msvideo";
    if(strcmp(dot,".mov")==0 || strcmp(dot,".qt")==0)
        return "video/quicktime";
    if(strcmp(dot,".mpeg")==0 || strcmp(dot,".mpe")==0)
        return "video/mpeg";
    if(strcmp(dot,".vrml")==0 || strcmp(dot,".wrl")==0)
        return "model/vrml";
    if(strcmp(dot,".midi")==0 || strcmp(dot,".mid")==0)
        return "audio/midi";
    if(strcmp(dot,".mp3")==0)
        return "audio/mpeg";
    if(strcmp(dot,".ogg")==0)
        return "application/ogg";
    if(strcmp(dot,".pac")==0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}



// int Read(int fd,char* buf,size_t len)
// {
//     int readsize=read(fd,buf,sizeof(buf));
//     if(readsize==-1){
//         cout<<"read errno="<<errno<<endl;
//         if(errno == EAGAIN || errno == EINTR) {
//             perror("read error");
//             return -1;
//         }
//         else{
//             perror("read error");
//             exit(1);
//         }
//     }
//     else if(readsize==0){
//         return 0;
//     }
//     return readsize; 
// }

// int Write(int cfd,char* buf,int readsize)
// {
//     int total=0;
//     int sendsize;
//     sleep(1);
//     while(total < readsize){
//         sendsize=send(cfd,buf+total,readsize-total, MSG_NOSIGNAL);
//         if(sendsize==-1){
//             cout<<"errno="<<errno<<endl;
//             if(errno == EAGAIN || errno == EINTR) { //errno == SIGPIPE
//                 continue;
//             }
//             else if(errno == EPIPE){ //broken pipe
//                 perror("send error");
//                 break;
//             }
//             else{
//                 perror("send error");
//                 exit(1);
//             }
//         }
//         else if(sendsize==0) break;
//         total +=sendsize;
//     }
//     return total;   
// }

#endif