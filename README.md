# tinyWebServer
这是一个基于Linux环境下C++轻量级服务器

* 使用**数据库连接池** + **线程池** + **非阻塞**socket + epoll(**ET模式**) + **事件处理Reactor网络模型**
* 使用**主从状态机**解析HTTP请求报文，同时支持解析**GET**和**POST**请求。从状态机每次提取请求报文中的一行数据，主状态机完成对行数据的解析
* 实现**异步日志系统**，记录服务器运行状态和客户端请求的资源数据
* 通过访问数据库可以实现web端用户**注册**、**登录**功能，可以请求服务器的**图片和音视频文件**
* 使用Webbench进行压力测试，可以实现**上万的并发连接**数据交换

# 压力测试
* 虚拟机配置  
![image](https://github.com/userYXJ/tinyWebServer/assets/113999864/044d285d-ee2c-41e9-a104-875629047d7a)

* 测试结果  
![image](https://github.com/userYXJ/tinyWebServer/assets/113999864/2878686a-be1d-45ce-ba95-c735385ec48b)

