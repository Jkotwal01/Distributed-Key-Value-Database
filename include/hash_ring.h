#pragma once
#include "utils.h"
#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <shared_mutex>

// ─── HashRing ─────────────────────────────────────────────────────────────────
// Consistent hash ring using MurmurHash3 + 150 virtual nodes per physical node.
// O(log n) key lookup via std::map (sorted by hash).
// ─────────────────────────────────────────────────────────────────────────────
class HashRing {
public:
    static constexpr int VIRTUAL_NODES = 150;

    HashRing() = default;

    void add_node(const NodeInfo& node);
    void remove_node(int node_id);
    void update_node_status(int node_id, bool alive);

    // Returns primary owner of 'key'
    std::optional<NodeInfo> get_primary(const std::string& key) const;

    // Returns 'rf' distinct nodes (primary + replicas) for 'key'
    std::vector<NodeInfo> get_replicas(const std::string& key, int rf) const;

    // Returns all known nodes
    std::vector<NodeInfo> get_all_nodes() const;

    // Number of physical nodes registered
    size_t node_count() const;

private:
    uint32_t murmur3(const std::string& data) const;
    uint32_t vnode_hash(int node_id, int vnode_idx) const;

    mutable std::shared_mutex mu_;
    std::map<uint32_t, NodeInfo> ring_;          // hash → NodeInfo (vnodes)
    std::map<int, NodeInfo>      nodes_by_id_;   // id  → NodeInfo (physical)
};
