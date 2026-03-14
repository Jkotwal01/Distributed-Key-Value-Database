#include "utils.h"
#include "nlohmann/json.hpp"
#include <stdexcept>

using json = nlohmann::json;

std::string msgtype_to_str(MsgType t) {
    switch (t) {
        case MsgType::PUT:          return "PUT";
        case MsgType::GET:          return "GET";
        case MsgType::DEL:          return "DELETE";
        case MsgType::REPLICATE:    return "REPLICATE";
        case MsgType::HEARTBEAT:    return "HEARTBEAT";
        case MsgType::ELECTION:     return "ELECTION";
        case MsgType::LEADER:       return "LEADER";
        case MsgType::ACK:          return "ACK";
        case MsgType::SYNC_REQUEST: return "SYNC_REQUEST";
        case MsgType::SYNC_DATA:    return "SYNC_DATA";
        default:                    return "UNKNOWN";
    }
}

MsgType str_to_msgtype(const std::string& s) {
    if (s == "PUT")          return MsgType::PUT;
    if (s == "GET")          return MsgType::GET;
    if (s == "DELETE")       return MsgType::DEL;
    if (s == "REPLICATE")    return MsgType::REPLICATE;
    if (s == "HEARTBEAT")    return MsgType::HEARTBEAT;
    if (s == "ELECTION")     return MsgType::ELECTION;
    if (s == "LEADER")       return MsgType::LEADER;
    if (s == "ACK")          return MsgType::ACK;
    if (s == "SYNC_REQUEST") return MsgType::SYNC_REQUEST;
    if (s == "SYNC_DATA")    return MsgType::SYNC_DATA;
    return MsgType::UNKNOWN;
}

std::string serialize_message(const Message& m) {
    json j;
    j["type"]      = msgtype_to_str(m.type);
    j["key"]       = m.key;
    j["value"]     = m.value;
    j["node_id"]   = m.node_id;
    j["term"]      = m.term;
    j["timestamp"] = m.timestamp;
    j["success"]   = m.success;
    j["error"]     = m.error;

    // Encode kv_pairs as array of [key, value] pairs for SYNC_DATA
    if (!m.kv_pairs.empty()) {
        json arr = json::array();
        for (auto& [k, v] : m.kv_pairs) {
            arr.push_back({k, v});
        }
        j["kv_pairs"] = arr;
    }

    return j.dump() + "\n";
}

Message deserialize_message(const std::string& json_str) {
    Message m;
    try {
        json j = json::parse(json_str);
        m.type      = str_to_msgtype(j.value("type", "UNKNOWN"));
        m.key       = j.value("key", "");
        m.value     = j.value("value", "");
        m.node_id   = j.value("node_id", 0);
        m.term      = j.value("term", 0);
        m.timestamp = j.value("timestamp", int64_t(0));
        m.success   = j.value("success", false);
        m.error     = j.value("error", "");

        if (j.contains("kv_pairs")) {
            for (auto& pair : j["kv_pairs"]) {
                m.kv_pairs.emplace_back(pair[0].get<std::string>(),
                                        pair[1].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        m.type  = MsgType::UNKNOWN;
        m.error = std::string("parse error: ") + e.what();
    }
    return m;
}
