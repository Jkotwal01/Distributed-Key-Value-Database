/**
 * test_storage.cpp
 * Unit tests for StorageEngine — PUT/GET/DEL correctness, WAL recovery,
 * and concurrent read/write stress test.
 *
 * Build & run:
 *   cmake --build build --target test_storage
 *   ./build/bin/test_storage
 */

#include "storage_engine.h"
#include "utils.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <filesystem>
#include <string>
#include <atomic>

namespace fs = std::filesystem;

// ─── Tiny test harness ────────────────────────────────────────────────────────
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

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

// ─── Test 1: Basic PUT / GET / DEL ────────────────────────────────────────────
void test_basic_crud() {
    std::cout << "\n--- test_basic_crud ---\n";
    fs::remove("./data/test_wal1.log");
    StorageEngine db("./data/test_wal1.log");

    db.put("key1", "value1");
    db.put("key2", "value2");

    ASSERT_TRUE(db.get("key1").has_value());
    ASSERT_EQ(db.get("key1").value(), "value1");
    ASSERT_EQ(db.get("key2").value(), "value2");
    ASSERT_TRUE(!db.get("key_no_exist").has_value());

    ASSERT_TRUE(db.del("key1"));
    ASSERT_TRUE(!db.get("key1").has_value());
    ASSERT_TRUE(!db.del("key1"));   // double-delete returns false

    ASSERT_EQ(db.size(), size_t(1));
}

// ─── Test 2: WAL Recovery ─────────────────────────────────────────────────────
void test_wal_recovery() {
    std::cout << "\n--- test_wal_recovery ---\n";
    fs::path wal = "./data/test_wal2.log";
    fs::remove(wal);

    {
        StorageEngine db(wal.string());
        for (int i = 0; i < 50; ++i)
            db.put("k" + std::to_string(i), "v" + std::to_string(i));
        db.del("k0");
        db.del("k1");
    }
    // Recover
    StorageEngine db2(wal.string());
    ASSERT_EQ(db2.size(), size_t(48));
    ASSERT_TRUE(!db2.get("k0").has_value());
    ASSERT_TRUE(!db2.get("k1").has_value());
    ASSERT_EQ(db2.get("k2").value(), "v2");
    ASSERT_EQ(db2.get("k49").value(), "v49");
}

// ─── Test 3: Concurrent stress ────────────────────────────────────────────────
void test_concurrent_rw() {
    std::cout << "\n--- test_concurrent_rw ---\n";
    fs::remove("./data/test_wal3.log");
    StorageEngine db("./data/test_wal3.log");

    // Pre-populate
    for (int i = 0; i < 100; ++i)
        db.put("shared:" + std::to_string(i), "init");

    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    // 5 writer threads
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&db, &errors, t](){
            for (int i = 0; i < 200; ++i) {
                try {
                    db.put("shared:" + std::to_string(i % 100),
                           "writer" + std::to_string(t));
                } catch (...) { ++errors; }
            }
        });
    }
    // 5 reader threads
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&db, &errors](){
            for (int i = 0; i < 200; ++i) {
                try {
                    db.get("shared:" + std::to_string(i % 100));
                } catch (...) { ++errors; }
            }
        });
    }

    for (auto& t : threads) t.join();
    ASSERT_EQ(errors.load(), 0);
    std::cout << "  Concurrent stress: no race errors\n";
}

// ─── Test 4: Bulk batch put ────────────────────────────────────────────────────
void test_batch_put() {
    std::cout << "\n--- test_batch_put ---\n";
    fs::remove("./data/test_wal4.log");
    StorageEngine db("./data/test_wal4.log");

    std::vector<std::pair<std::string,std::string>> pairs;
    for (int i = 0; i < 30; ++i)
        pairs.push_back({"bk" + std::to_string(i), "bv" + std::to_string(i)});
    db.put_batch(pairs);

    ASSERT_EQ(db.size(), size_t(30));
    ASSERT_EQ(db.get("bk0").value(), "bv0");
    ASSERT_EQ(db.get("bk29").value(), "bv29");
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== StorageEngine Unit Tests ===\n";
    test_basic_crud();
    test_wal_recovery();
    test_concurrent_rw();
    test_batch_put();

    std::cout << "\n=== Results: " << g_passed << " passed, "
              << g_failed << " failed ===\n";
    return g_failed == 0 ? 0 : 1;
}
