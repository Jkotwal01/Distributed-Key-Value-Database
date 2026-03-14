#pragma once
#include <string>
#include <sstream>
#include <chrono>
#include <mutex>
#include <iostream>
#include <optional>
#include <functional>
#include <vector>

// ─── Log Level ────────────────────────────────────────────────────────────────
enum class LogLevel { DBG, INF, WRN, ERR };

// ─── Global Logger ────────────────────────────────────────────────────────────
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    void set_level(LogLevel lvl) { min_level_ = lvl; }
    void set_node_id(int id) { node_id_ = id; }

    void log(LogLevel lvl, const std::string& msg) {
        if (lvl < min_level_) return;
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::system_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()).count();
        std::cout << "[" << ms << "] [Node " << node_id_ << "] ["
                  << level_str(lvl) << "] " << msg << "\n" << std::flush;
    }

private:
    Logger() = default;
    std::mutex mu_;
    LogLevel   min_level_{LogLevel::INF};
    int        node_id_{0};

    static const char* level_str(LogLevel l) {
        switch (l) {
            case LogLevel::DBG: return "DEBUG";
            case LogLevel::INF: return "INFO ";
            case LogLevel::WRN: return "WARN ";
            case LogLevel::ERR: return "ERROR";
        }
        return "?";
    }
};

#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DBG, msg)
#define LOG_INFO(msg)  Logger::instance().log(LogLevel::INF,  msg)
#define LOG_WARN(msg)  Logger::instance().log(LogLevel::WRN,  msg)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERR, msg)

// ─── Message Types ────────────────────────────────────────────────────────────
enum class MsgType {
    PUT,
    GET,
    DEL,
    REPLICATE,
    HEARTBEAT,
    ELECTION,
    LEADER,
    ACK,
    SYNC_REQUEST,
    SYNC_DATA,
    UNKNOWN
};

// ─── Node Info ────────────────────────────────────────────────────────────────
struct NodeInfo {
    int         id;
    std::string host;
    int         port;
    bool        alive{true};

    std::string addr() const { return host + ":" + std::to_string(port); }
    bool operator==(const NodeInfo& o) const { return id == o.id; }
};

// ─── Message ──────────────────────────────────────────────────────────────────
struct Message {
    MsgType     type{MsgType::UNKNOWN};
    std::string key;
    std::string value;
    int         node_id{0};
    int         term{0};
    int64_t     timestamp{0};
    bool        success{false};
    std::string error;

    // For SYNC_DATA: list of key-value pairs
    std::vector<std::pair<std::string,std::string>> kv_pairs;
};

// ─── Timestamp helper ─────────────────────────────────────────────────────────
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// Serialization declarations (implemented in utils.cpp)
std::string  serialize_message(const Message& m);
Message      deserialize_message(const std::string& json_str);
std::string  msgtype_to_str(MsgType t);
MsgType      str_to_msgtype(const std::string& s);
