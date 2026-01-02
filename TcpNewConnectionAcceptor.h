#ifndef _TcpNewConnectionAcceptor_
#define _TcpNewConnectionAcceptor_

#include <pthread.h>

class TcpServerController;

class TcpNewConnectionAcceptor
{
    private:
        int accept_fd;
        pthread_t* accept_new_conn_thread;
    public:
        TcpServerController* tcp_ctrl;

        TcpNewConnectionAcceptor(TcpServerController*);
        ~TcpNewConnectionAcceptor();

        void StartTcpNewConnectionAcceptorThread();
        void StartTcpNewConnectionAcceptorThreadInterval();

};

#endif // _TcpNewConnectionAcceptor_