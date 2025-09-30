#pragma once

#include <iostream>
#include <string>
#include <cstdint>
#include <ctime>
#include <arpa/inet.h>
#include "request.h"

namespace PrintUtils {
    /**
     * @brief ### Converts uint32_t IP to string format
     * @param ip_addr IP address in host byte order
     * @return IP string (e.g., "192.168.1.1")
     */
    std::string ip_to_string(uint32_t ip_addr);
    
    /**
     * @brief ### Prints current time in YYYY-MM-DD HH:MM:SS format
     */
    void print_time();
    
    /**
     * @brief ### Prints server state information
     * @param transactions Number of transactions
     * @param transferred Total transferred amount
     * @param balance Total balance
     */
    void print_server_state(uint32_t transactions, uint32_t transferred, uint32_t balance);
    
    /**
     * @brief ### Prints transfer information
     * @param server_ip Server IP address
     * @param seq_number Sequence number
     * @param dst_ip Destination IP
     * @param value Transfer value
     * @param new_balance New balance after transfer
     */
    void print_transfer(uint32_t server_ip, uint32_t seq_number, uint32_t dst_ip, uint32_t value, uint32_t new_balance);

    /**
     * @brief ### Prints request information with client details
     * @param req The request to print
     * @param is_duplicate Whether the request is a duplicate
     */
    void print_request(const Request& req, bool is_duplicate = false);
}