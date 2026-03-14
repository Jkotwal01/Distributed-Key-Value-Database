#include "hash_ring.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <set>

// ─── MurmurHash3 (31-bit, fast, well-distributed) ────────────────────────────
uint32_t HashRing::murmur3(const std::string& data) const {
    const uint32_t c1 = 0xcc9e2d51u;
    const uint32_t c2 = 0x1b873593u;
    const uint32_t seed = 42u;
    const int len = static_cast<int>(data.size());
    const auto* key = reinterpret_cast<const uint8_t*>(data.data());

    uint32_t h = seed;
    const int nblocks = len / 4;
    const auto* blocks = reinterpret_cast<const uint32_t*>(key);

    for (int i = 0; i < nblocks; ++i) {
        uint32_t k = blocks[i];
        k *= c1; k = (k << 15) | (k >> 17); k *= c2;
        h ^= k;  h = (h << 13) | (h >> 19); h = h * 5 + 0xe6546b64u;
    }

    const uint8_t* tail = key + nblocks * 4;
    uint32_t k = 0;
    switch (len & 3) {
        case 3: k ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
        case 2: k ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
        case 1: k ^= tail[0];
                k *= c1; k = (k << 15) | (k >> 17); k *= c2; h ^= k;
    }
    h ^= static_cast<uint32_t>(len);
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13;
    h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

uint32_t HashRing::vnode_hash(int node_id, int vnode_idx) const {
    // Unique string per virtual node: "nodeID-vnodeIDX"
    std::string label = "node" + std::to_string(node_id)
                      + "-vn"  + std::to_string(vnode_idx);
    return murmur3(label);
}

// ─── Add / Remove ─────────────────────────────────────────────────────────────

void HashRing::add_node(const NodeInfo& node) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    nodes_by_id_[node.id] = node;
    for (int i = 0; i < VIRTUAL_NODES; ++i) {
        uint32_t h = vnode_hash(node.id, i);
        ring_[h] = node;
    }
}

void HashRing::remove_node(int node_id) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    nodes_by_id_.erase(node_id);
    // Erase all virtual nodes belonging to this physical node
    for (auto it = ring_.begin(); it != ring_.end(); ) {
        if (it->second.id == node_id) it = ring_.erase(it);
        else ++it;
    }
}

void HashRing::update_node_status(int node_id, bool alive) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    auto it = nodes_by_id_.find(node_id);
    if (it == nodes_by_id_.end()) return;
    it->second.alive = alive;
    // Update all virtual node entries
    for (auto& [h, ni] : ring_) {
        if (ni.id == node_id) ni.alive = alive;
    }
}

// ─── Lookup ───────────────────────────────────────────────────────────────────

std::optional<NodeInfo> HashRing::get_primary(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    if (ring_.empty()) return std::nullopt;

    uint32_t h = murmur3(key);
    auto it = ring_.lower_bound(h);
    if (it == ring_.end()) it = ring_.begin();   // wrap-around

    // Walk clockwise until we find an alive node
    auto start = it;
    do {
        if (it->second.alive) return it->second;
        ++it;
        if (it == ring_.end()) it = ring_.begin();
    } while (it != start);

    return std::nullopt;   // all nodes dead
}

std::vector<NodeInfo> HashRing::get_replicas(const std::string& key, int rf) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<NodeInfo> result;
    if (ring_.empty()) return result;

    uint32_t h = murmur3(key);
    auto it = ring_.lower_bound(h);
    if (it == ring_.end()) it = ring_.begin();

    std::set<int> seen_ids;
    auto start_it = it;
    do {
        if (it->second.alive && seen_ids.find(it->second.id) == seen_ids.end()) {
            result.push_back(it->second);
            seen_ids.insert(it->second.id);
            if (static_cast<int>(result.size()) == rf) break;
        }
        ++it;
        if (it == ring_.end()) it = ring_.begin();
    } while (it != start_it);

    return result;
}

std::vector<NodeInfo> HashRing::get_all_nodes() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<NodeInfo> out;
    out.reserve(nodes_by_id_.size());
    for (auto& [id, ni] : nodes_by_id_) out.push_back(ni);
    return out;
}

size_t HashRing::node_count() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return nodes_by_id_.size();
}
