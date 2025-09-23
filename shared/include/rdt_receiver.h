#include <netinet/in.h>

class RDTReceiver {
public:
    RDTReceiver();
    ~RDTReceiver();
private:
    static void* look_for_data(void * args);
    int sockfd;
    char buffer[256];
	struct sockaddr_in serv_addr, cli_addr;
	char buf[256];
};