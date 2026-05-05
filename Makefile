CC=g++
CFLAGS=-g -std=c++11 -Wall -Wextra -pthread
TARGET=chat_app tcp_client tcp_client_string_sender
LIBS=-lpthread -lsqlite3

OBJS=TcpClientDBManager.o \
     TcpClientServiceManager.o \
     TcpNewConnectionAcceptor.o \
     TcpServerController.o \
     network_utils.o \
     TcpClient.o \
     UserDatabase.o

all: $(TARGET)

tcp_client: tcp_client.o
	$(CC) $(CFLAGS) tcp_client.o -o tcp_client

chat_app: chat_app.o $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) chat_app.o -o chat_app $(LIBS)

tcp_client_string_sender: tcp_client_string_sender.o
	$(CC) $(CFLAGS) tcp_client_string_sender.o -o tcp_client_string_sender

tcp_client_string_sender.o: tcp_client_string_sender.cpp
	$(CC) $(CFLAGS) -c tcp_client_string_sender.cpp -o tcp_client_string_sender.o

tcp_client.o: tcp_client.cpp
	$(CC) $(CFLAGS) -c tcp_client.cpp -o tcp_client.o

chat_app.o: chat_app.cpp
	$(CC) $(CFLAGS) -c chat_app.cpp -o chat_app.o

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

UserDatabase.o: UserDatabase.cpp
	$(CC) $(CFLAGS) -c UserDatabase.cpp -o UserDatabase.o

clean:
	rm -f *.o *.exe chat_app tcp_client tcp_client_string_sender

rebuild: clean all
