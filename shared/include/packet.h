#include "request.h"
#include "response.h"
#define DESC 1
#define REQ 2
#define DESC_ACK 3
#define REQ_ACK 4

class Packet {
public:
    Packet(int seq, Request r) : type(REQ), seq_number(seq) { data.req = r; }
    Packet(int seq, Response r) : type(REQ_ACK), seq_number(seq) { data.resp = r; }
    int32_t src_ip;
    int32_t dest_ip;
    int type;
    int seq_number = 0;
    union {
        Request req;
        Response resp;
    } data;
};