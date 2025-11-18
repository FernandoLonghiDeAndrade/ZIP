#pragma once
#include <cstdint>

/**
 * @brief ### Packet types for Client-Server communication.
 */
enum ClientPacketType : uint8_t {
    // Discovery phase
    CLIENT_DISCOVERY,          ///< Client -> Server: Request to register/discover server
    CLIENT_DISCOVERY_ACK,      ///< Server -> Client: Confirmation with client's current state
    
    // Transaction phase
    TRANSACTION_REQUEST,       ///< Client -> Server: Request to transfer funds
    
    // Transaction responses (mutually exclusive)
    TRANSACTION_ACK,           ///< Server -> Client: Transaction successful
    INSUFFICIENT_BALANCE_ACK,  ///< Server -> Client: Transaction rejected (not enough funds)
    INVALID_CLIENT_ACK,        ///< Server -> Client: Transaction rejected (destination doesn't exist)
    ERROR_ACK,                 ///< Server -> Client: Transaction rejected (server error)
};

/**
 * @brief ### Payload for transaction request packets (Client -> Server).
 * 
 * Used for TRANSACTION_REQUEST packets.
 * Contains destination and amount for fund transfer.
 */
struct RequestPayload {
    uint32_t id;                ///< Sequence number for idempotency
    uint32_t destination_ip;    ///< Destination client's IP (network byte order, use ntohl() to read)
    uint32_t value;             ///< Amount to transfer (non-negative, validated by server)
};

/**
 * @brief ### Payload for acknowledgment packets (Server -> Client).
 * 
 * Used for CLIENT_DISCOVERY_ACK and TRANSACTION_ACK packets.
 * Contains sender's updated balance after transaction.
 */
struct ReplyPayload {
    uint32_t id;            ///< Sequence number for idempotency
    uint32_t new_balance;   ///< Sender's balance after transaction (or current balance for CLIENT_DISCOVERY_ACK)
};

/**
 * @brief ### Main packet structure for all Client-Server communication.
 * 
 * Payload is a union (only one variant is valid depending on packet type).
 */
struct ClientPacket {
    ClientPacketType type;  ///< Discriminator for the Payload union (determines which variant is valid)
    
    /**
     * @brief ### Tagged union containing packet-specific data.
     * 
     * Only one member is valid at a time
     */
    union {
        RequestPayload request; ///< Valid for TRANSACTION_REQUEST packets
        ReplyPayload reply;     ///< Valid for CLIENT_DISCOVERY_ACK and TRANSACTION_ACK packets
    } payload;

    /**
     * @brief ### Constructor to initialize packet with type.
     * 
     * @param type Packet type to set.
     */
    ClientPacket(ClientPacketType type) : type(type) {}

    ClientPacket() = default;

    /**
     * @brief ### Factory method for creating request packets (client -> server).
     * 
     * Use this for:
     * - CLIENT_DISCOVERY (dest_ip and value are ignored, can be 0)
     * - TRANSACTION_REQUEST (dest_ip and value are required)
     * 
     * @param request_id Client's sequence number
     * @param dest_ip Destination client IP in network byte order
     * @param value Amount to transfer
     * @return Initialized request packet ready to send
     */
    static ClientPacket create_request(uint32_t request_id, uint32_t dest_ip, uint32_t value) {
        ClientPacket p(TRANSACTION_REQUEST);
        p.payload.request.id = request_id;
        p.payload.request.destination_ip = dest_ip;
        p.payload.request.value = value;
        return p;
    }

    /**
     * @brief ### Factory method for creating reply packets (server -> client).
     * 
     * Use this for all ACK types:
     * - CLIENT_DISCOVERY_ACK: balance = current client balance
     * - TRANSACTION_ACK: balance = sender's new balance after debit
     * - INSUFFICIENT_BALANCE_ACK: balance = sender's balance (unchanged)
     * - INVALID_CLIENT_ACK: balance = sender's balance (unchanged)
     * - ERROR_ACK: balance = sender's balance (unchanged)
     * 
     * @param type ACK packet type (CLIENT_DISCOVERY_ACK, TRANSACTION_ACK, etc.)
     * @param request_id Echo of the request_id from the original request
     * @param balance Client's balance (interpretation depends on ACK type, see above)
     * @return Initialized reply packet ready to send
     */
    static ClientPacket create_reply(ClientPacketType type, uint32_t request_id, uint32_t balance) {
        ClientPacket p(type);
        p.payload.reply.id = request_id;
        p.payload.reply.new_balance = balance;
        return p;
    }

    /**
     * @brief ### Calculate exact packet size for efficient UDP transmission.
     * 
     * @return Size in bytes of the serialized packet (type + active payload).
     */
    size_t size() const {
        size_t base_size = sizeof(ClientPacketType);
        
        switch (type) {            
            case TRANSACTION_REQUEST:
                return base_size + sizeof(RequestPayload);
            
            case CLIENT_DISCOVERY_ACK:
            case TRANSACTION_ACK:
                return base_size + sizeof(ReplyPayload);
            
            default:
                return base_size;
        }
    }
};
