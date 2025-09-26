#include "request_type.h"

struct Request {
    RequestType type = RequestType::DISCOVERY;
    int32_t src_ip = 0;
    int16_t src_port = 0;
    int32_t dest_addr = 0;
    int seq_number = 0;
    int value = 0;
};
