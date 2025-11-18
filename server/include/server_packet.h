#pragma once
#include <cstdint>
#include "udp_socket.h"
#include "client_info.h"

/**
 * @brief ### Packet types for Server-Server communication.
 */
enum ServerPacketType : uint8_t {
    // Server discovery
    SERVER_DISCOVERY = 128, ///< Server -> Leader: New server tries to discover leader
    SERVER_DISCOVERY_ACK,   ///< Leader -> Server: Response to server discovery
    CLIENT_INFO,            ///< Leader -> Server: Information about one client
    SERVER_INFO,            ///< Leader -> Server: Information about one server
    SEQ_AND_STATS,          ///< Leader -> Server: Server seq_number and stats
    IDS,                    ///< Leader -> Server: Server and leader IDs

    // State synchronization
    STATE_SYNC_REQUEST,     ///< Server -> Leader: Request full leader server state (when packet loss is detected)
    STATE_SYNC_ACK,         ///< Leader -> Server: Full state transfer in response to STATE_SYNC_REQUEST
    NEW_SERVER_SYNC,        ///< Server -> Other Servers: Notify all servers about new server
    NEW_TRANSACTION_SYNC,   ///< Server -> Other Servers: Notify all servers about new transaction
    
    // Bully leader election
    ELECTION_REQUEST,       ///< Server -> Server (higher ID): Initiate election
    ELECTION_REQUEST_ACK,   ///< Server (higher ID) -> Server: Response to election request
    COORDINATOR,            ///< Server -> Other Servers: Notify all servers about new leader
};

/**
 * @brief ### Information about one Client.
 */
struct ClientInfoPayload {
    uint32_t sync_seq_number;
    uint32_t ip;
    ClientInfo info;
};

/**
 * @brief ### Information about one Server.
 */
struct ServerInfoPayload {
    uint32_t sync_seq_number;
    SocketAddress server;
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
 * @brief ### Server and leader IDs.
 */
struct IDsPayload {
    uint32_t sync_seq_number;
    uint32_t server_id;
    uint32_t leader_id;
};

/**
 * @brief ### New Server notification payload.
 */
struct NewServerSyncPayload {
    uint32_t seq_number;
    SocketAddress new_server;
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
 * @brief ### Coordinator notification payload.
 */
struct CoordinatorPayload {
    uint32_t seq_number;
    uint32_t id;
};

/**
 * @brief ### Packet for server-to-server communication.
 * 
 * Variable size depending on payload type.
 */
struct ServerPacket {
    ServerPacketType type;
    
    union {
        ClientInfoPayload client;
        ServerInfoPayload server;
        SeqAndStatsPayload state;
        IDsPayload ids;
        NewServerSyncPayload new_server;
        NewTransactionSyncPayload new_transaction;
        CoordinatorPayload leader;
    } payload;

    /**
     * @brief ### Constructor to initialize packet with type.
     * 
     * @param type Packet type to set.
     */
    ServerPacket(ServerPacketType type) : type(type), payload(0) {}

    ServerPacket() = default;

    static ServerPacket create_client_info(uint32_t sync_seq, uint32_t ip, ClientInfo info) {
        ServerPacket p(CLIENT_INFO);
        p.payload.client.sync_seq_number = sync_seq;
        p.payload.client.ip = ip;
        p.payload.client.info = info;
        return p;
    }

    static ServerPacket create_server_info(uint32_t sync_seq, SocketAddress server) {
        ServerPacket p(SERVER_INFO);
        p.payload.server.sync_seq_number = sync_seq;
        p.payload.server.server = server;
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

    static ServerPacket create_ids(uint32_t sync_seq, uint32_t server_id, uint32_t leader_id) {
        ServerPacket p(IDS);
        p.payload.ids.sync_seq_number = sync_seq;
        p.payload.ids.server_id = server_id;
        p.payload.ids.leader_id = leader_id;
        return p;
    }

    static ServerPacket create_new_server_sync(uint32_t seq_number, SocketAddress new_server) {
        ServerPacket p(NEW_SERVER_SYNC);
        p.payload.new_server.seq_number = seq_number;
        p.payload.new_server.new_server = new_server;
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

    static ServerPacket create_coordinator(uint32_t seq_number, uint32_t leader_id) {
        ServerPacket p(COORDINATOR);
        p.payload.leader.seq_number = seq_number;
        p.payload.leader.id = leader_id;
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
            
            case IDS:
                return base_size + sizeof(IDsPayload);
            
            case NEW_SERVER_SYNC:
                return base_size + sizeof(NewServerSyncPayload);
            
            case NEW_TRANSACTION_SYNC:
                return base_size + sizeof(NewTransactionSyncPayload);
            
            case COORDINATOR:
                return base_size + sizeof(CoordinatorPayload);
            
            default:
                return base_size;
        }
    }
};