#pragma once
#include <cstdint>
#include <variant>
#include "udp_socket.h"
#include "client_info.h"

/**
 * @brief ### Packet types for Server-Server communication.
 */
enum ServerPacketType : uint8_t {
    // Server discovery
    SERVER_DISCOVERY = 128, ///< Server -> Leader: New server tries to discover leader
    SERVER_DISCOVERY_ACK,   ///< Leader -> Server: Response to server discovery

    // State synchronization
    STATE_SYNC_REQUEST,     ///< Server -> Leader: Request full leader server state (when packet loss is detected)
    STATE_SYNC_ACK,         ///< Leader -> Server: Full state transfer in response to STATE_SYNC_REQUEST
    NEW_SERVER,             ///< Server -> Other Servers: Notify all servers about new server
    NEW_TRANSACTION,        ///< Server -> Other Servers: Notify all servers about new transaction
    
    // Bully leader election
    ELECTION_REQUEST,       ///< Server -> Server (higher ID): Initiate election
    ELECTION_REQUEST_ACK,   ///< Server (higher ID) -> Server: Response to election request
    COORDINATOR,            ///< Server -> Other Servers: Notify all servers about new leader
};

struct IDs {
    uint32_t server_id;
    uint32_t leader_id;
};

struct State {
    uint32_t seq_number;
    uint32_t num_transactions;
    uint64_t total_transferred;
    uint64_t total_balance;
};

struct Client {
    uint32_t ip;
    ClientInfo info;
};

/**
 * @brief ### Leader state discovery ACK payload.
 */
struct ServerDiscoveryAckPayload {
    uint32_t discovery_seq_number;
    std::variant<
        IDs,
        State,
        Client,
        SocketAddress
    > data;
};

/**
 * @brief ### Leader state replication payload.
 */
struct StateSyncAckPayload {
    uint32_t sync_seq_number;
    std::variant<
        State,
        Client,
        SocketAddress
    > data;
};

/**
 * @brief ### New Server notification payload.
 */
struct NewServerPayload {
    uint32_t seq_number;
    SocketAddress new_server;
};

/**
 * @brief ### New Transaction notification payload.
 */
struct NewTransactionPayload {
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
 * State sync can be large (sent via TCP or fragmented UDP).
 */
struct ServerPacket {
    ServerPacketType type;
    
    std::variant<
        ServerDiscoveryAckPayload,
        StateSyncAckPayload,
        NewServerPayload,
        NewTransactionPayload,
        CoordinatorPayload
    > payload;

    static ServerPacket create_discovery_ack_ids(uint32_t discovery_seq_number, uint32_t server_id, uint32_t leader_id) {
        ServerPacket p;
        p.type = SERVER_DISCOVERY_ACK;
        std::get<ServerDiscoveryAckPayload>(p.payload).discovery_seq_number = discovery_seq_number;
        std::get<ServerDiscoveryAckPayload>(p.payload).data = IDs{server_id, leader_id};
        return p;
    }

    static ServerPacket create_discovery_ack_state(uint32_t discovery_seq_number, uint32_t seq_number, uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance) {
        ServerPacket p;
        p.type = SERVER_DISCOVERY_ACK;
        std::get<ServerDiscoveryAckPayload>(p.payload).discovery_seq_number = discovery_seq_number;
        std::get<ServerDiscoveryAckPayload>(p.payload).data = State{seq_number, num_transactions, total_transferred, total_balance};
        return p;
    }

    static ServerPacket create_discovery_ack_client(uint32_t discovery_seq_number, uint32_t ip, ClientInfo info) {
        ServerPacket p;
        p.type = SERVER_DISCOVERY_ACK;
        std::get<ServerDiscoveryAckPayload>(p.payload).discovery_seq_number = discovery_seq_number;
        std::get<ServerDiscoveryAckPayload>(p.payload).data = Client{ip, info};
        return p;
    }

    static ServerPacket create_discovery_ack_server(uint32_t discovery_seq_number, SocketAddress server) {
        ServerPacket p;
        p.type = SERVER_DISCOVERY_ACK;
        std::get<ServerDiscoveryAckPayload>(p.payload).discovery_seq_number = discovery_seq_number;
        std::get<ServerDiscoveryAckPayload>(p.payload).data = SocketAddress{server};
        return p;
    }

    static ServerPacket create_state_sync_ack_state(uint32_t sync_seq_number, uint32_t seq_number, uint32_t num_transactions, uint64_t total_transferred, uint64_t total_balance) {
        ServerPacket p;
        p.type = STATE_SYNC_ACK;
        std::get<StateSyncAckPayload>(p.payload).sync_seq_number = sync_seq_number;
        std::get<StateSyncAckPayload>(p.payload).data = State{seq_number, num_transactions, total_transferred, total_balance};
        return p;
    }

    static ServerPacket create_state_sync_ack_client(uint32_t sync_seq_number, uint32_t ip, ClientInfo info) {
        ServerPacket p;
        p.type = STATE_SYNC_ACK;
        std::get<StateSyncAckPayload>(p.payload).sync_seq_number = sync_seq_number;
        std::get<StateSyncAckPayload>(p.payload).data = Client{ip, info};
        return p;
    }

    static ServerPacket create_state_sync_ack_server(uint32_t sync_seq_number, SocketAddress server) {
        ServerPacket p;
        p.type = STATE_SYNC_ACK;
        std::get<StateSyncAckPayload>(p.payload).sync_seq_number = sync_seq_number;
        std::get<StateSyncAckPayload>(p.payload).data = SocketAddress{server};
        return p;
    }

    static ServerPacket create_new_server(uint32_t seq_number, SocketAddress new_server) {
        ServerPacket p;
        p.type = NEW_SERVER;
        std::get<NewServerPayload>(p.payload).seq_number = seq_number;
        std::get<NewServerPayload>(p.payload).new_server = new_server;
        return p;
    }

    static ServerPacket create_new_transaction(uint32_t seq_number, uint32_t src_ip, uint32_t dest_ip, uint32_t value) {
        ServerPacket p;
        p.type = NEW_TRANSACTION;
        std::get<NewTransactionPayload>(p.payload).seq_number = seq_number;
        std::get<NewTransactionPayload>(p.payload).src_ip = src_ip;
        std::get<NewTransactionPayload>(p.payload).dest_ip = dest_ip;
        std::get<NewTransactionPayload>(p.payload).value = value;
        return p;
    }

    static ServerPacket create_coordinator(uint32_t seq_number, uint32_t leader_id) {
        ServerPacket p;
        p.type = COORDINATOR;
        std::get<CoordinatorPayload>(p.payload).seq_number = seq_number;
        std::get<CoordinatorPayload>(p.payload).id = leader_id;
        return p;
    }
};