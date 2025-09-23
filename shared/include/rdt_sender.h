class RDTSender {
public:
    RDTSender();
    ~RDTSender();
    void send(char data[]);
private:
    int sockfd;
    char buffer[256];
    struct hostent *server;
    struct sockaddr_in serv_addr, from;
};