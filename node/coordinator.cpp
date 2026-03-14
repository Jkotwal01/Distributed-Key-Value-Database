#include "coordinator.h"
#include <sstream>

Coordinator::Coordinator(int my_id, NodeInfo self_info,
                         HashRing&           ring,
                         StorageEngine&      storage,
                         ReplicationManager& replication,
                         LeaderElection&     election)
    : my_id_(my_id), self_(std::move(self_info)),
      ring_(ring), storage_(storage),
      replication_(replication), election_(election)
{}

// ─── Main dispatcher ──────────────────────────────────────────────────────────
Message Coordinator::handle(const Message& msg) {
    switch (msg.type) {
        case MsgType::PUT:          return handle_put(msg);
        case MsgType::GET:          return handle_get(msg);
        case MsgType::DEL:          return handle_delete(msg);
        case MsgType::REPLICATE:    return replication_.handle_replicate(msg);
        case MsgType::SYNC_REQUEST: return replication_.handle_sync_request(msg);
        case MsgType::SYNC_DATA:
            replication_.handle_sync_data(msg);
            return make_ack(true);
        case MsgType::HEARTBEAT: {
            Message ack;
            ack.type    = MsgType::ACK;
            ack.node_id = my_id_;
            ack.success = true;
            return ack;
        }
        case MsgType::ELECTION:
        case MsgType::LEADER: {
            auto resp = election_.handle_message(msg);
            if (resp) return *resp;
            return make_ack(true);
        }
        default:
            return make_error("Unknown message type");
    }
}

// ─── PUT ─────────────────────────────────────────────────────────────────────
Message Coordinator::handle_put(const Message& msg) {
    auto primary_opt = ring_.get_primary(msg.key);
    if (!primary_opt) return make_error("No available node for key: " + msg.key);

    NodeInfo primary = *primary_opt;

    if (primary.id == my_id_) {
        storage_.put(msg.key, msg.value);
        auto replicas = ring_.get_replicas(msg.key, REPLICATION_FACTOR);
        replication_.replicate(msg.key, msg.value, MsgType::PUT, replicas);
        LOG_INFO("PUT key=" + msg.key + " stored locally + replicated");
        return make_ack(true);
    } else {
        return forward_to(primary, msg);
    }
}

// ─── GET ─────────────────────────────────────────────────────────────────────
Message Coordinator::handle_get(const Message& msg) {
    auto primary_opt = ring_.get_primary(msg.key);
    if (!primary_opt) return make_error("No available node for key: " + msg.key);

    NodeInfo primary = *primary_opt;

    if (primary.id == my_id_) {
        auto val = storage_.get(msg.key);
        if (!val) return make_error("Key not found: " + msg.key);
        return make_ack(true, *val);
    } else {
        return forward_to(primary, msg);
    }
}

// ─── DELETE ──────────────────────────────────────────────────────────────────
Message Coordinator::handle_delete(const Message& msg) {
    auto primary_opt = ring_.get_primary(msg.key);
    if (!primary_opt) return make_error("No available node for key: " + msg.key);

    NodeInfo primary = *primary_opt;

    if (primary.id == my_id_) {
        bool removed = storage_.del(msg.key);
        if (!removed) return make_error("Key not found: " + msg.key);
        auto replicas = ring_.get_replicas(msg.key, REPLICATION_FACTOR);
        replication_.replicate(msg.key, "", MsgType::DEL, replicas);
        LOG_INFO("DELETE key=" + msg.key);
        return make_ack(true);
    } else {
        return forward_to(primary, msg);
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
Message Coordinator::forward_to(const NodeInfo& target, const Message& msg) {
    auto resp = send_message(target.host, target.port, msg);
    if (!resp) {
        LOG_WARN("Primary node " + std::to_string(target.id) + " unreachable, trying replicas");
        auto replicas = ring_.get_replicas(msg.key, REPLICATION_FACTOR);
        for (auto& r : replicas) {
            if (r.id == target.id) continue;
            if (r.id == my_id_) {
                // Serve locally if we're a replica
                if (msg.type == MsgType::GET) {
                    auto val = storage_.get(msg.key);
                    if (val) return make_ack(true, *val);
                }
                continue;
            }
            resp = send_message(r.host, r.port, msg);
            if (resp && resp->success) return *resp;
        }
        // Last resort: check own storage
        if (msg.type == MsgType::GET) {
            auto val = storage_.get(msg.key);
            if (val) return make_ack(true, *val);
        }
        return make_error("All nodes for key unavailable");
    }
    return *resp;
}

Message Coordinator::make_error(const std::string& err) const {
    Message m;
    m.type    = MsgType::ACK;
    m.node_id = my_id_;
    m.success = false;
    m.error   = err;
    return m;
}

Message Coordinator::make_ack(bool ok, const std::string& val) const {
    Message m;
    m.type    = MsgType::ACK;
    m.node_id = my_id_;
    m.success = ok;
    m.value   = val;
    return m;
}

void Coordinator::on_node_failure(int node_id) {
    ring_.update_node_status(node_id, false);
    LOG_WARN("Node " + std::to_string(node_id) + " marked DOWN in hash ring");
    if (election_.get_leader() == node_id) {
        LOG_INFO("Leader failed — triggering re-election");
        election_.trigger_election();
    }
}

void Coordinator::on_node_recovery(int node_id) {
    ring_.update_node_status(node_id, true);
    LOG_INFO("Node " + std::to_string(node_id) + " is BACK ONLINE");
}

std::vector<NodeInfo> Coordinator::get_cluster_status() const {
    return ring_.get_all_nodes();
}
