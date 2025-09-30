#pragma once

enum RequestType {
    DISCOVERY, TRANSFER
};

struct Request {
    Request() = default;
    Request(
        RequestType type, uint32_t dst_ip, uint32_t seq_number = 0,
        uint32_t value = 0, uint32_t src_ip = 0, uint32_t src_port = 0
    ) : type(type), dst_ip(dst_ip), seq_number(seq_number),
        value(value), src_ip(src_ip), src_port(src_port) {}
    
    RequestType type;
    uint32_t dst_ip;
    int32_t seq_number;
    uint32_t src_ip;
    int32_t value;
    uint16_t src_port;
};
