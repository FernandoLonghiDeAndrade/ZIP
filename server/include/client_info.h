#pragma once
#include <cstdint>

/// Initial balance assigned to newly discovered clients (prevents negative balances on first transaction)
constexpr uint32_t CLIENT_INITIAL_BALANCE = 100;

/**
 * @brief ### Per-client state maintained by the server.
 * 
 * Tracks request ID for idempotency (duplicate detection) and current balance.
 * Stored in LockedMap with per-entry reader-writer locks for concurrent access.
 */
struct ClientInfo {
    uint32_t last_processed_request_id = 0;		///< Last processed request ID (for duplicate detection)
                                                ///< 0 = no requests processed yet
    uint32_t balance = CLIENT_INITIAL_BALANCE;  ///< Current balance (decremented on send, incremented on receive)
};