
all:server

# 编译器和选项
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

# 头文件路径
INCDIR = log http mysql_conn_pool thread_pool timer 

# 库文件路径
LIBDIR = /usr/lib64/mysql
LIBS = -lmysqlclient -lpthread -ldl -lssl -lcrypto -lresolv -lm -lrt


# 目标可执行文件
TARGET = server


# 生成目标文件 
./obj/log.o:log/log.cpp 
	g++ $(CXXFLAGS)  -c $< -o $@
./obj/http.o:http/http_conn.cpp 
	g++ $(CXXFLAGS)  -c $< -o $@
./obj/mysql_conn_pool.o:mysql_conn_pool/conn_pool.cpp 
	g++ $(CXXFLAGS)  -c $< -o $@
./obj/timer.o:timer/timer.cpp 
	g++ $(CXXFLAGS)  -c $< -o $@

#生成目标
$(TARGET):main.cpp thread_pool/thread_pool.hpp file_type.h include.h ./obj/log.o ./obj/http.o ./obj/mysql_conn_pool.o ./obj/timer.o
	g++ $^ -o $@ -L$(LIBDIR) $(LIBS) -gdwarf-4 -gstrict-dwarf

# 清理目标文件和可执行文件 
clean: 
	rm -rf ./obj/* $(TARGET)
