#pragma once
#include <cstdint>
#include "udp_socket.h"
#include "client_info.h"

/**
 * @brief ### Packet types for Server-Server communication.
 */
enum ServerPacketType : uint8_t {
    // Server discovery or full state synchronization
    STATE_SYNC_REQUEST = 128,   ///< Server -> Leader: Request full leader server state (if new server or when packet loss is detected)
    STATE_SYNC_ACK,             ///< Leader -> Server: Response to STATE_SYNC_REQUEST
    SERVER_INFO,                ///< Leader -> Server: Information about one server
    CLIENT_INFO,                ///< Leader -> Server: Information about one client
    SEQ_AND_STATS,              ///< Leader -> Server: Server seq_number and stats

    // Server synchronization (new server or new transaction)
    NEW_SERVER_SYNC,        ///< Server -> Other Servers: Notify all servers about new server
    NEW_CLIENT_SYNC,        ///< Server -> Other Servers: Notify all servers about new client
    NEW_TRANSACTION_SYNC,   ///< Server -> Other Servers: Notify all servers about new transaction
    
    // Bully leader election
    PING_LEADER,            ///< Backup Server -> Leader: Ping to check if leader is alive
    PING_LEADER_ACK,        ///< Leader -> Backup Server: Response to PING_LEADER
    ELECTION_REQUEST,       ///< Server -> Server (lower ID): Initiate election
    ELECTION_REQUEST_ACK,   ///< Server (lower ID) -> Server: Response to election request
    COORDINATOR,            ///< Server -> Other Servers: Notify all servers about new leader
};

/**
 * @brief ### Information about one Server.
 */
struct ServerInfoPayload {
    uint32_t seq_number;
    SocketAddress addr;
};

/**
 * @brief ### Information about one Client.
 */
struct ClientInfoPayload {
    uint32_t seq_number;
    uint32_t ip;
    ClientInfo info;
};

/**
 * @brief ### Server sequence number and statistics.
 */
struct SeqAndStatsPayload {
    uint32_t sync_seq_number;
    uint32_t seq_number;
    uint32_t num_transactions;
    uint64_t total_transferred;
    uint64_t total_balance;
};

/**
 * @brief ### New Transaction notification payload.
 */
struct NewTransactionSyncPayload {
    uint32_t seq_number;
    uint32_t src_ip;    ///< Transaction source
    uint32_t dest_ip;   ///< Transaction destination
    uint32_t value;     ///< Transfer amount
};

/**
 * @brief ### Packet for server-to-server communication.
 * 
 * Variable size depending on payload type.
 */
struct ServerPacket {
    ServerPacketType type;
    
    union {
        ServerInfoPayload server;
        ClientInfoPayload client;
        SeqAndStatsPayload state;
        NewTransactionSyncPayload new_transaction;
    } payload;

    ServerPacket() : payload{0} {}
    
    /**
     * @brief ### Constructor to initialize packet with type.
     * 
     * @param type Packet type to set.
     */
    ServerPacket(ServerPacketType type) : type(type), payload{0} {}

    static ServerPacket create_server_info(uint32_t sync_seq, SocketAddress server_addr) {
        ServerPacket p(SERVER_INFO);
        p.payload.server.seq_number = sync_seq;
        p.payload.server.addr = server_addr;
        return p;
    }

    static ServerPacket create_client_info(uint32_t sync_seq, uint32_t ip, ClientInfo info) {
        ServerPacket p(CLIENT_INFO);
        p.payload.client.seq_number = sync_seq;
        p.payload.client.ip = ip;
        p.payload.client.info = info;
        return p;
    }

    static ServerPacket create_seq_and_stats(uint32_t sync_seq, uint32_t seq_number, uint32_t num_transactions, 
                                             uint64_t total_transferred, uint64_t total_balance) {
        ServerPacket p(SEQ_AND_STATS);
        p.payload.state.sync_seq_number = sync_seq;
        p.payload.state.seq_number = seq_number;
        p.payload.state.num_transactions = num_transactions;
        p.payload.state.total_transferred = total_transferred;
        p.payload.state.total_balance = total_balance;
        return p;
    }

    static ServerPacket create_new_server_sync(uint32_t seq_number, SocketAddress server_addr) {
        ServerPacket p(SERVER_INFO);
        p.payload.server.seq_number = seq_number;
        p.payload.server.addr = server_addr;
        return p;
    }

    static ServerPacket create_new_client_sync(uint32_t seq_number, uint32_t ip, ClientInfo info) {
        ServerPacket p(CLIENT_INFO);
        p.payload.client.seq_number = seq_number;
        p.payload.client.ip = ip;
        p.payload.client.info = info;
        return p;
    }

    static ServerPacket create_new_transaction_sync(uint32_t seq_number, uint32_t src_ip, uint32_t dest_ip, uint32_t value) {
        ServerPacket p(NEW_TRANSACTION_SYNC);
        p.payload.new_transaction.seq_number = seq_number;
        p.payload.new_transaction.src_ip = src_ip;
        p.payload.new_transaction.dest_ip = dest_ip;
        p.payload.new_transaction.value = value;
        return p;
    }

    /**
     * @brief ### Calculate exact packet size for efficient UDP transmission.
     * 
     * @return Size in bytes of the serialized packet (type + active payload variant).
     */
    size_t size() const {
        size_t base_size = sizeof(ServerPacketType);
        
        switch (type) {
            case CLIENT_INFO:
                return base_size + sizeof(ClientInfoPayload);
            
            case SERVER_INFO:
                return base_size + sizeof(ServerInfoPayload);
            
            case SEQ_AND_STATS:
                return base_size + sizeof(SeqAndStatsPayload);
            
            case NEW_SERVER_SYNC:
                return base_size + sizeof(ServerInfoPayload);
            
            case NEW_CLIENT_SYNC:
                return base_size + sizeof(ClientInfoPayload);

            case NEW_TRANSACTION_SYNC:
                return base_size + sizeof(NewTransactionSyncPayload);

            default:
                return base_size;
        }
    }
};