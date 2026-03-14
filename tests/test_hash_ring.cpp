/**
 * test_hash_ring.cpp
 * Unit tests for HashRing — deterministic routing, replica selection,
 * node failure handling, and distribution balance.
 */

#include "hash_ring.h"
#include "utils.h"

#include <cassert>
#include <iostream>
#include <unordered_map>
#include <set>
#include <random>
#include <string>

static int g_passed = 0, g_failed = 0;

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "FAIL [" << __LINE__ << "]: " << #expr << "\n"; \
            ++g_failed; \
        } else { \
            std::cout << "PASS [" << __LINE__ << "]: " << #expr << "\n"; \
            ++g_passed; \
        } \
    } while(0)

#define ASSERT_EQ(a, b)  ASSERT_TRUE((a) == (b))
#define ASSERT_GE(a, b)  ASSERT_TRUE((a) >= (b))
#define ASSERT_LE(a, b)  ASSERT_TRUE((a) <= (b))

// ─── Test 1: All keys route to a node ────────────────────────────────────────
void test_all_keys_routed() {
    std::cout << "\n--- test_all_keys_routed ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});

    int unrouted = 0;
    for (int i = 0; i < 1000; ++i) {
        auto n = ring.get_primary("key" + std::to_string(i));
        if (!n) ++unrouted;
    }
    ASSERT_EQ(unrouted, 0);
}

// ─── Test 2: Consistent routing (same key → same node) ───────────────────────
void test_deterministic_routing() {
    std::cout << "\n--- test_deterministic_routing ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});

    std::string key = "user:abc123";
    auto first = ring.get_primary(key);
    ASSERT_TRUE(first.has_value());
    for (int i = 0; i < 20; ++i) {
        auto n = ring.get_primary(key);
        ASSERT_TRUE(n.has_value());
        ASSERT_EQ(n->id, first->id);
    }
}

// ─── Test 3: Node removal causes minimal key migration ───────────────────────
void test_minimal_migration_on_removal() {
    std::cout << "\n--- test_minimal_migration_on_removal ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});

    const int N = 3000;
    std::unordered_map<std::string, int> before;
    for (int i = 0; i < N; ++i) {
        std::string k = "k" + std::to_string(i);
        auto n = ring.get_primary(k);
        if (n) before[k] = n->id;
    }

    ring.remove_node(2);

    int moved = 0;
    for (int i = 0; i < N; ++i) {
        std::string k = "k" + std::to_string(i);
        auto n = ring.get_primary(k);
        if (!n) continue;
        if (before.count(k) && before[k] != 2 && n->id != before[k]) ++moved;
    }

    // With consistent hashing: only keys on node 2 should move (~1/3 of all keys)
    // Keys on node 1 and 3 should NOT move → movement of non-node2 keys < 5%
    double ratio = static_cast<double>(moved) / N;
    std::cout << "  Non-node2 keys moved: " << moved << "/" << N
              << " (" << ratio*100 << "%)\n";
    ASSERT_TRUE(ratio < 0.05);   // less than 5% of unrelated keys should move
}

// ─── Test 4: Replica selection returns RF distinct nodes ─────────────────────
void test_replica_selection() {
    std::cout << "\n--- test_replica_selection ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});

    for (int i = 0; i < 100; ++i) {
        auto replicas = ring.get_replicas("key" + std::to_string(i), 3);
        ASSERT_EQ(static_cast<int>(replicas.size()), 3);
        // All IDs must be distinct
        std::set<int> ids;
        for (auto& r : replicas) ids.insert(r.id);
        ASSERT_EQ(static_cast<int>(ids.size()), 3);
    }
}

// ─── Test 5: Dead node is skipped in routing ─────────────────────────────────
void test_dead_node_skipped() {
    std::cout << "\n--- test_dead_node_skipped ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});
    ring.update_node_status(2, false);

    for (int i = 0; i < 500; ++i) {
        auto n = ring.get_primary("key" + std::to_string(i));
        ASSERT_TRUE(n.has_value());
        ASSERT_TRUE(n->id != 2);
    }
}

// ─── Test 6: Load balance (virtual nodes) ────────────────────────────────────
void test_load_balance() {
    std::cout << "\n--- test_load_balance ---\n";
    HashRing ring;
    ring.add_node({1, "localhost", 8001, true});
    ring.add_node({2, "localhost", 8002, true});
    ring.add_node({3, "localhost", 8003, true});

    std::unordered_map<int,int> counts;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        auto n = ring.get_primary("randomkey:" + std::to_string(i));
        if (n) counts[n->id]++;
    }
    // Ideal: ~3333 per node. Accept 2.5× imbalance with vnodes
    int max_count = 0, min_count = N;
    for (auto& [id, c] : counts) {
        max_count = std::max(max_count, c);
        min_count = std::min(min_count, c);
    }
    double imbalance = static_cast<double>(max_count) / std::max(min_count, 1);
    std::cout << "  Load: max=" << max_count << " min=" << min_count
              << " imbalance=" << imbalance << "x\n";
    ASSERT_TRUE(imbalance < 2.5);
}

int main() {
    std::cout << "=== HashRing Unit Tests ===\n";
    test_all_keys_routed();
    test_deterministic_routing();
    test_minimal_migration_on_removal();
    test_replica_selection();
    test_dead_node_skipped();
    test_load_balance();

    std::cout << "\n=== Results: " << g_passed << " passed, "
              << g_failed << " failed ===\n";
    return g_failed == 0 ? 0 : 1;
}
