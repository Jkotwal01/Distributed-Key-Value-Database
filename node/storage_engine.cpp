#include "storage_engine.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

StorageEngine::StorageEngine(const std::string& wal_path)
    : wal_path_(wal_path)
{
    // Ensure data directory exists
    fs::path p(wal_path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    // Recover any existing WAL before opening for append
    recover();

    // Open WAL in append mode
    wal_file_.open(wal_path_, std::ios::app);
    if (!wal_file_.is_open()) {
        LOG_WARN("Could not open WAL file: " + wal_path_ + ". Running without persistence.");
    }
}

StorageEngine::~StorageEngine() {
    std::lock_guard<std::mutex> lk(wal_mutex_);
    if (wal_file_.is_open()) wal_file_.close();
}

// ─── Core CRUD ────────────────────────────────────────────────────────────────

void StorageEngine::put(const std::string& key, const std::string& value) {
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        store_[key] = value;
    }
    append_wal("PUT", key, value);
}

std::optional<std::string> StorageEngine::get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

bool StorageEngine::del(const std::string& key) {
    bool removed = false;
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        auto it = store_.find(key);
        if (it != store_.end()) {
            store_.erase(it);
            removed = true;
        }
    }
    if (removed) append_wal("DEL", key, "");
    return removed;
}

// ─── Bulk ─────────────────────────────────────────────────────────────────────

std::vector<std::string> StorageEngine::get_all_keys() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    std::vector<std::string> keys;
    keys.reserve(store_.size());
    for (auto& [k, _] : store_) keys.push_back(k);
    return keys;
}

std::vector<std::pair<std::string,std::string>> StorageEngine::get_all_pairs() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return {store_.begin(), store_.end()};
}

void StorageEngine::put_batch(const std::vector<std::pair<std::string,std::string>>& pairs) {
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        for (auto& [k, v] : pairs) store_[k] = v;
    }
    for (auto& [k, v] : pairs) append_wal("PUT", k, v);
}

size_t StorageEngine::size() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return store_.size();
}

// ─── WAL ─────────────────────────────────────────────────────────────────────

void StorageEngine::append_wal(const std::string& op,
                                const std::string& key,
                                const std::string& value)
{
    std::lock_guard<std::mutex> lk(wal_mutex_);
    if (!wal_file_.is_open()) return;

    // Format: OP|key|value|timestamp_ms
    wal_file_ << op << "|" << key << "|" << value << "|" << now_ms() << "\n";
    wal_file_.flush();
}

void StorageEngine::recover() {
    std::ifstream f(wal_path_);
    if (!f.is_open()) return;   // first run — no WAL yet

    std::string line;
    size_t recovered = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // Parse: OP|key|value|timestamp
        std::istringstream ss(line);
        std::string op, key, value, ts;
        if (!std::getline(ss, op, '|'))    continue;
        if (!std::getline(ss, key, '|'))   continue;
        if (!std::getline(ss, value, '|')) continue;

        if (op == "PUT") {
            store_[key] = value;
            ++recovered;
        } else if (op == "DEL") {
            store_.erase(key);
            ++recovered;
        }
    }
    if (recovered > 0) {
        LOG_INFO("WAL recovery: replayed " + std::to_string(recovered) + " entries, "
                 + std::to_string(store_.size()) + " keys loaded.");
    }
}
