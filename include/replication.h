#pragma once
#include "utils.h"
#include "storage_engine.h"
#include <vector>
#include <functional>

// ─── ReplicationManager ───────────────────────────────────────────────────────
// Handles async replication to replica nodes and data sync on node recovery.
// ─────────────────────────────────────────────────────────────────────────────
class ReplicationManager {
public:
    ReplicationManager(int my_id, StorageEngine& storage)
        : my_id_(my_id), storage_(storage) {}

    // Async-replicate a PUT or DELETE operation to the given replica nodes.
    // Spawns a detached thread per replica — fire-and-forget.
    void replicate(const std::string& key,
                   const std::string& value,
                   MsgType op,                    // PUT or DELETE
                   const std::vector<NodeInfo>& replicas);

    // Handle an incoming REPLICATE message (store to local engine).
    Message handle_replicate(const Message& msg);

    // Handle incoming SYNC_REQUEST: respond with all local KV pairs.
    Message handle_sync_request(const Message& msg);

    // Handle incoming SYNC_DATA: load all received KV pairs into local storage.
    void handle_sync_data(const Message& msg);

    // Active sync: request all KV pairs from a peer (called on startup/recovery).
    void sync_from_peer(const NodeInfo& peer);

private:
    int            my_id_;
    StorageEngine& storage_;
};
