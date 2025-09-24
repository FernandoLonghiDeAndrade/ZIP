#include <netinet/in.h>

class RDTReceiver {
public:
    RDTReceiver();
    ~RDTReceiver();
    bool receive(char data[]);
private:
    int sockfd;
    bool expected_seq_number = 0;
	struct sockaddr_in serv_addr, cli_addr;
};