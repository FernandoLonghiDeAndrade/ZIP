#pragma once
#include "packet.h"
#include <cstdint>

/**
 * @brief ### Utility functions for formatted console output with timestamps.
 * 
 * All functions automatically prepend the current timestamp in format: "YYYY-MM-DD HH:MM:SS".
 * Used by both client and server for consistent logging format.
 */
namespace PrintUtils {
    /**
     * @brief ### [Server] Prints bank statistics summary line.
     * 
     * Output format: "YYYY-MM-DD HH:MM:SS num_transactions X total_transferred Y total_balance Z"
     * Called at server startup (with zeros) and after each successful transaction.
     * 
     * @param num_transactions Total successful transactions processed (excludes duplicates and failures)
     * @param total_transferred Cumulative sum of all transaction values (never decreases)
     * @param total_balance Sum of all client balances (invariant: should equal num_clients * INITIAL_BALANCE)
     */
    void print_server_state(uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance);

    /**
     * @brief ### [Server] Prints transaction request details and updated bank state.
     * 
     * Output format (2 lines):
     * Line 1: "YYYY-MM-DD HH:MM:SS client <IP> [DUP!!] id_req X dest <IP> value Y"
     * Line 2: "num_transactions X total_transferred Y total_balance Z"
     * 
     * Called after processing TRANSACTION_REQUEST, before sending ACK.
     * Marks duplicate requests with "DUP!!" flag (idempotency check).
     * 
     * @param client_ip Source client's IP in host byte order (will be converted to dotted notation)
     * @param packet The TRANSACTION_REQUEST packet (contains dest_ip and value)
     * @param is_duplicate True if request_id <= last_processed_request_id (duplicate retransmission)
     * @param num_transactions Current transaction count (unchanged if duplicate)
     * @param total_transferred Current total transferred (unchanged if duplicate)
     * @param total_balance Current total balance (invariant: remains constant across transactions)
     */
    void print_request(uint32_t client_ip, const Packet& packet, bool is_duplicate, uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance);

    /**
     * @brief ### [Client] Prints transaction result after receiving TRANSACTION_ACK.
     * 
     * Output format: "YYYY-MM-DD HH:MM:SS server <IP> id_req X dest <IP> value Y new_balance Z"
     * Called only after successful transaction (not for error ACKs).
     * 
     * @param server_ip Server's IP in network byte order (will be converted to dotted notation)
     * @param request_id Echo of the request_id sent in original TRANSACTION_REQUEST
     * @param dest_ip Destination client IP in network byte order (from original request)
     * @param value Transaction amount (from original request)
     * @param new_balance Client's updated balance after debit (from TRANSACTION_ACK payload)
     */
    void print_reply(uint32_t server_ip, uint32_t request_id, uint32_t dest_ip, uint32_t value, uint32_t new_balance);
    
    /**
     * @brief ### [Client] Prints server discovery confirmation.
     * 
     * Output format: "YYYY-MM-DD HH:MM:SS server_addr <IP>"
     * Called after receiving DISCOVERY_ACK (discovery phase complete).
     * 
     * @param server_ip Server's IP in network byte order (will be converted to dotted notation)
     */
    void print_discovery_reply(uint32_t server_ip);
}
