#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>
#include <utility>
#include <shared_mutex>
#include <fstream>
#include <mutex>

// ─── StorageEngine ────────────────────────────────────────────────────────────
// Thread-safe key-value store backed by in-memory map + append-only WAL
// (Write-Ahead Log) for crash recovery.
//
// Thread safety: shared_mutex — multiple concurrent readers, exclusive writers.
// ─────────────────────────────────────────────────────────────────────────────
class StorageEngine {
public:
    explicit StorageEngine(const std::string& wal_path = "./data/wal.log");
    ~StorageEngine();

    // Core CRUD
    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key) const;
    bool del(const std::string& key);

    // Bulk operations
    std::vector<std::string> get_all_keys() const;
    std::vector<std::pair<std::string,std::string>> get_all_pairs() const;
    void put_batch(const std::vector<std::pair<std::string,std::string>>& pairs);

    // Persistence
    void recover();   // replay WAL on startup
    size_t size() const;

private:
    void append_wal(const std::string& op,
                    const std::string& key,
                    const std::string& value);

    std::string wal_path_;
    mutable std::shared_mutex rw_mutex_;
    std::unordered_map<std::string, std::string> store_;
    std::ofstream wal_file_;
    mutable std::mutex wal_mutex_;
};
