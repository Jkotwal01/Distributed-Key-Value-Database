#include "replication.h"
#include "tcp_client.h"
#include <thread>

void ReplicationManager::replicate(const std::string& key,
                                   const std::string& value,
                                   MsgType op,
                                   const std::vector<NodeInfo>& replicas)
{
    Message repl;
    repl.type      = MsgType::REPLICATE;
    repl.key       = key;
    repl.value     = value;
    repl.node_id   = my_id_;
    repl.timestamp = now_ms();
    // Encode operation (PUT or DELETE) in the error field as a tag
    repl.error     = (op == MsgType::PUT) ? "PUT" : "DEL";

    for (auto& r : replicas) {
        if (r.id == my_id_) continue;  // don't replicate to self
        NodeInfo peer = r;
        Message  msg  = repl;
        std::thread([peer, msg](){
            send_and_forget(peer.host, peer.port, msg);
        }).detach();
    }
    LOG_DEBUG("Replicated key=" + key + " to " + std::to_string(replicas.size()) + " nodes");
}

Message ReplicationManager::handle_replicate(const Message& msg) {
    if (msg.error == "DEL") {
        storage_.del(msg.key);
        LOG_DEBUG("Replica DEL key=" + msg.key);
    } else {
        storage_.put(msg.key, msg.value);
        LOG_DEBUG("Replica PUT key=" + msg.key);
    }
    Message ack;
    ack.type    = MsgType::ACK;
    ack.node_id = my_id_;
    ack.success = true;
    return ack;
}

Message ReplicationManager::handle_sync_request(const Message& /*msg*/) {
    Message resp;
    resp.type     = MsgType::SYNC_DATA;
    resp.node_id  = my_id_;
    resp.kv_pairs = storage_.get_all_pairs();
    resp.success  = true;
    LOG_INFO("Serving SYNC_DATA: " + std::to_string(resp.kv_pairs.size()) + " key-value pairs");
    return resp;
}

void ReplicationManager::handle_sync_data(const Message& msg) {
    if (msg.kv_pairs.empty()) return;
    storage_.put_batch(msg.kv_pairs);
    LOG_INFO("Sync complete: " + std::to_string(msg.kv_pairs.size()) + " pairs loaded from node " + std::to_string(msg.node_id));
}

void ReplicationManager::sync_from_peer(const NodeInfo& peer) {
    LOG_INFO("Requesting sync from peer node " + std::to_string(peer.id));
    Message req;
    req.type    = MsgType::SYNC_REQUEST;
    req.node_id = my_id_;

    auto resp = send_message(peer.host, peer.port, req);
    if (!resp || resp->type != MsgType::SYNC_DATA) {
        LOG_WARN("Sync from node " + std::to_string(peer.id) + " failed");
        return;
    }
    handle_sync_data(*resp);
}
