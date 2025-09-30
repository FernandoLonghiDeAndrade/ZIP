#pragma once

struct Reply {
    Reply(uint32_t server_ip = 0, uint32_t server_port = 0, int32_t seq_number = 0, uint32_t new_balance = 0) :
        server_ip(server_ip), server_port(server_port), seq_number(seq_number), new_balance(new_balance) {}

    uint32_t server_ip;
    uint32_t server_port;
    int32_t seq_number;
    uint32_t new_balance;
};
