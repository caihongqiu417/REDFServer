CC=g++
CFLAGS=-g
TARGET:testapp.exe tcp_client.exe  # ← 添加客户端目标
LIBS=-lpthread
OBJS=TcpClientDBManager.o 	   \
	TcpClientServiceManager.o 	\
	TcpNewConnectionAcceptor.o 	 \
	TcpServerController.o 		  \
	network_utils.o					\
	TcpClient.o

# 添加客户端编译规则
tcp_client.exe: tcp_client.o
	${CC} ${CFLAGS} tcp_client.o -o tcp_client.exe

testapp.exe:testapp.o ${OBJS}
	${CC} ${CFLAGS} ${OBJS} testapp.o -o testapp.exe ${LIBS}

testapp.o:testapp.cpp
	${CC} ${CFLAGS} -c testapp.cpp -o testapp.o

# 客户端编译规则
tcp_client.o: tcp_client.cpp
	${CC} ${CFLAGS} -c tcp_client.cpp -o tcp_client.o



TcpClientDBManager.o:TcpClientDBManager.cpp
	${CC} ${CFLAGS} -c TcpClientDBManager.cpp -o TcpClientDBManager.o

TcpClientServiceManager.o:TcpClientServiceManager.cpp
	${CC} ${CFLAGS} -c TcpClientServiceManager.cpp -o TcpClientServiceManager.o

TcpNewConnectionAcceptor.o:TcpNewConnectionAcceptor.cpp
	${CC} ${CFLAGS} -c TcpNewConnectionAcceptor.cpp -o TcpNewConnectionAcceptor.o

TcpServerController.o:TcpServerController.cpp
	${CC} ${CFLAGS} -c TcpServerController.cpp -o TcpServerController.o

network_utils.o:network_utils.cpp
	${CC} ${CFLAGS} -c network_utils.cpp -o network_utils.o

TcpClient.o:TcpClient.cpp
	${CC} ${CFLAGS} -c TcpClient.cpp -o TcpClient.o

clean:
	rm -f *.o
	rm -f *.exe  # ← 这里也改一下