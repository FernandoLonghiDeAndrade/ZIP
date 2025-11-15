#pragma once
#include <cstdint>

static constexpr uint8_t MAX_SERVERS = 16;    ///< Maximum number of servers supported

/**
 * @brief ### Defines the type of message being sent in a Packet.
 * 
 * Values are powers of 2 to allow bitmasking in future extensions.
 * Currently unused for bitmasking, but provides clear separation between types.
 */
enum PacketType : uint8_t {
    // Discovery phase
    CLIENT_DISCOVERY = 1,                  ///< Client -> Server: Request to register/discover server
    CLIENT_DISCOVERY_ACK = 2,              ///< Server -> Client: Confirmation with client's current state
    
    // Transaction phase
    TRANSACTION_REQUEST = 4,        ///< Client -> Server: Request to transfer funds
    
    // Transaction responses (mutually exclusive)
    TRANSACTION_ACK = 8,            ///< Server -> Client: Transaction successful
    INSUFFICIENT_BALANCE_ACK = 16,  ///< Server -> Client: Transaction rejected (not enough funds)
    INVALID_CLIENT_ACK = 32,        ///< Server -> Client: Transaction rejected (destination doesn't exist)
    ERROR_ACK = 64,                 ///< Server -> Client: Transaction rejected (server error)

    SERVER_DISCOVERY = 128,                  ///< Backup Server -> Clients: Request to discover current leader server
    SERVER_DISCOVERY_ACK = 129,              ///< Leader Server -> Backup Servers: Confirmation of
    SERVER_LIST_UPDATE = 130                  ///< Leader Server -> Backup Servers: Update list of backup servers
};

/**
 * @brief ### Payload for transaction request packets (client -> server).
 * 
 * Used when packet.type == TRANSACTION_REQUEST.
 * Contains destination and amount for fund transfer.
 */
struct RequestPayload {
    uint32_t destination_ip;    ///< Destination client's IP (network byte order, use ntohl() to read)
    uint32_t value;             ///< Amount to transfer (non-negative, validated by server)
};

/**
 * @brief ### Payload for acknowledgment packets (server -> client).
 * 
 * Used for all ACK packet types (DISCOVERY_ACK, TRANSACTION_ACK, etc.).
 * Contains sender's updated balance after transaction.
 */
struct ReplyPayload {
    uint32_t new_balance;       ///< Sender's balance after transaction (or current balance for DISCOVERY_ACK)
                                ///< For error ACKs, contains balance before failed transaction attempt
};

/**
 * @brief ### Information about a server in the system.
 * 
 * Used for server discovery and leader election among backup servers.
 */
struct ServerEntry {
    uint32_t ip;        ///< Server IP in network byte order (use ntohl() to read)
    uint16_t port;      ///< Server port in network byte order (use ntohs() to read)
    uint32_t id;        ///< Unique server ID for leader election
};

/**
 * @brief ### Payload for server discovery packets.
 * 
 * Used when packet.type == SERVER_DISCOVERY or SERVER_DISCOVERY_ACK.
 * Contains information about available servers for leader election.
 */
struct ServerDiscoveryPayload {
    uint32_t sequence_counter;  ///< Monotonically increasing counter for leader election
    uint32_t server_id;         ///< Unique server ID (for tie-breaking in elections)
};

/**
 * @brief ### Payload for server list update packets.
 * 
 * Used when packet.type == SERVER_LIST_UPDATE.
 * Contains updated list of backup servers.
 */
struct ServerListUpdatePayload {
    uint8_t server_count;                ///< Number of valid entries in 'servers'
    ServerEntry servers[MAX_SERVERS];    ///< Fixed list of available servers
};

/**
 * @brief ### Main packet structure for all client-server communication.
 * 
 * Fixed-size packet (safe for UDP transmission, no fragmentation needed).
 * 
 * Protocol semantics:
 * - request_id is managed by client (monotonically increasing, starts at 1)
 * - Server uses request_id for duplicate detection (idempotency)
 * - Payload is a union (only one variant is valid depending on packet type)
 * 
 * Size: 1 byte (type) + 4 bytes (request_id) + 8 bytes (union) = 13 bytes
 */
struct Packet {
    PacketType type;            ///< Discriminator for the Payload union (determines which variant is valid)
    uint32_t request_id;        ///< Sequence number for idempotency (0 = DISCOVERY, 1+ = transactions)
    
    /**
     * @brief ### Tagged union containing packet-specific data.
     * 
     * Only one member is valid at a time:
     * - request: valid when type is TRANSACTION_REQUEST
     * - reply: valid when type is any ACK variant
     * 
     * DISCOVERY packets don't use the payload (both variants are ignored).
     */
    union {
        RequestPayload request; ///< Valid for TRANSACTION_REQUEST packets
        ReplyPayload reply;     ///< Valid for all ACK packets (DISCOVERY_ACK, TRANSACTION_ACK, etc.)
        ServerDiscoveryPayload server_discovery; ///< Valid for SERVER_DISCOVERY and SERVER_DISCOVERY_ACK packets
        ServerListUpdatePayload server_list_update; ///< Valid for SERVER_LIST_UPDATE packets
    } payload;

    /**
     * @brief ### Factory method for creating request packets (client -> server).
     * 
     * Use this for:
     * - DISCOVERY (dest_ip and value are ignored, can be 0)
     * - TRANSACTION_REQUEST (dest_ip and value are required)
     * 
     * @param type Packet type (should be DISCOVERY or TRANSACTION_REQUEST)
     * @param request_id Client's sequence number (0 for DISCOVERY, 1+ for transactions)
     * @param dest_ip Destination client IP in network byte order (ignored for DISCOVERY)
     * @param value Amount to transfer (ignored for DISCOVERY)
     * @return Initialized request packet ready to send
     */
    static Packet create_request(PacketType type, uint32_t request_id, uint32_t dest_ip, uint32_t value) {
        Packet p;
        p.type = type;
        p.request_id = request_id;
        p.payload.request.destination_ip = dest_ip;
        p.payload.request.value = value;
        return p;
    }

    /**
     * @brief ### Factory method for creating reply packets (server -> client).
     * 
     * Use this for all ACK types:
     * - DISCOVERY_ACK: balance = current client balance
     * - TRANSACTION_ACK: balance = sender's new balance after debit
     * - INSUFFICIENT_BALANCE_ACK: balance = sender's balance (unchanged)
     * - INVALID_CLIENT_ACK: balance = sender's balance (unchanged)
     * - ERROR_ACK: balance = sender's balance (unchanged)
     * 
     * @param type ACK packet type (DISCOVERY_ACK, TRANSACTION_ACK, etc.)
     * @param request_id Echo of the request_id from the original request
     * @param balance Client's balance (interpretation depends on ACK type, see above)
     * @return Initialized reply packet ready to send
     */
    static Packet create_reply(PacketType type, uint32_t request_id, uint32_t balance) {
        Packet p;
        p.type = type;
        p.request_id = request_id;
        p.payload.reply.new_balance = balance;
        return p;
    }

    static Packet create_server_discovery_reply(uint32_t sequence_counter, uint32_t server_id) 
    {
        Packet p;
        p.type = SERVER_DISCOVERY_ACK;
        p.request_id = 0; // Not used for server discovery
        p.payload.server_discovery.sequence_counter = sequence_counter;
        p.payload.server_discovery.server_id = server_id;

        return p;
    }

    static Packet create_server_list_update_reply(
            uint8_t server_count, 
            ServerEntry servers[]) 
    {
        Packet p;
        p.type = SERVER_LIST_UPDATE;
        p.request_id = 0; // Not used for server discovery
        p.payload.server_list_update.server_count = server_count;

        for (uint8_t i = 0; i < server_count && i < MAX_SERVERS; ++i) {
            p.payload.server_list_update.servers[i].ip = servers[i].ip;
            p.payload.server_list_update.servers[i].port = servers[i].port;
            p.payload.server_list_update.servers[i].id = servers[i].id;
        }

        return p;
    }
};
