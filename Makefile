CC=g++
# 修正的编译选项
CFLAGS=-g -std=c++11 -Wall -Wextra -pthread
TARGET:testapp.exe tcp_client.exe
LIBS=-lpthread

OBJS=TcpClientDBManager.o \
     TcpClientServiceManager.o \
     TcpNewConnectionAcceptor.o \
     TcpServerController.o \
     network_utils.o \
     TcpClient.o

# 默认目标
all: $(TARGET)

# 客户端编译规则
tcp_client.exe: tcp_client.o
	$(CC) $(CFLAGS) tcp_client.o -o tcp_client.exe

# 服务器编译规则
testapp.exe: testapp.o $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) testapp.o -o testapp.exe $(LIBS)

# 客户端源文件编译
tcp_client.o: tcp_client.cpp
	$(CC) $(CFLAGS) -c tcp_client.cpp -o tcp_client.o

# 服务器源文件编译
testapp.o: testapp.cpp
	$(CC) $(CFLAGS) -c testapp.cpp -o testapp.o

TcpClientDBManager.o: TcpClientDBManager.cpp
	$(CC) $(CFLAGS) -c TcpClientDBManager.cpp -o TcpClientDBManager.o

TcpClientServiceManager.o: TcpClientServiceManager.cpp
	$(CC) $(CFLAGS) -c TcpClientServiceManager.cpp -o TcpClientServiceManager.o

TcpNewConnectionAcceptor.o: TcpNewConnectionAcceptor.cpp
	$(CC) $(CFLAGS) -c TcpNewConnectionAcceptor.cpp -o TcpNewConnectionAcceptor.o

TcpServerController.o: TcpServerController.cpp
	$(CC) $(CFLAGS) -c TcpServerController.cpp -o TcpServerController.o

network_utils.o: network_utils.cpp
	$(CC) $(CFLAGS) -c network_utils.cpp -o network_utils.o

TcpClient.o: TcpClient.cpp
	$(CC) $(CFLAGS) -c TcpClient.cpp -o TcpClient.o

# 清理规则
clean:
	rm -f *.o
	rm -f *.exe

# 重新编译所有
rebuild: clean all

# 运行测试
run: testapp.exe
	./testapp.exe

# 运行客户端
run-client: tcp_client.exe
	./tcp_client.exe 127.0.0.1 4000

.PHONY: all clean rebuild run run-client