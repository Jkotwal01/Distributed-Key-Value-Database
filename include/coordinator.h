#pragma once
#include "utils.h"
#include "storage_engine.h"
#include "hash_ring.h"
#include "tcp_client.h"
#include "replication.h"
#include "leader_election.h"
#include <memory>
#include <atomic>

// ─── Coordinator ─────────────────────────────────────────────────────────────
// The routing brain of each node.
// Routes PUT/GET/DELETE to the correct primary/replica using the hash ring.
// Manages replication factor and forwards requests when self is not primary.
// ─────────────────────────────────────────────────────────────────────────────
class Coordinator {
public:
    static constexpr int REPLICATION_FACTOR = 3;

    Coordinator(int my_id, NodeInfo self_info,
                HashRing&          ring,
                StorageEngine&     storage,
                ReplicationManager& replication,
                LeaderElection&    election);

    // Main dispatch — called by TcpServer handler for every incoming message
    Message handle(const Message& msg);

    // Callbacks from HeartbeatService
    void on_node_failure(int node_id);
    void on_node_recovery(int node_id);

    // Cluster status for /cluster/status endpoint
    std::vector<NodeInfo> get_cluster_status() const;
    int get_leader_id() const { return election_.get_leader(); }

private:
    Message handle_put(const Message& msg);
    Message handle_get(const Message& msg);
    Message handle_delete(const Message& msg);
    Message forward_to(const NodeInfo& target, const Message& msg);
    Message make_error(const std::string& err) const;
    Message make_ack(bool ok, const std::string& val = "") const;

    int                 my_id_;
    NodeInfo            self_;
    HashRing&           ring_;
    StorageEngine&      storage_;
    ReplicationManager& replication_;
    LeaderElection&     election_;
};
