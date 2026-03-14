#pragma once
#include "utils.h"
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>

// ─── HeartbeatService ─────────────────────────────────────────────────────────
// Sends HEARTBEAT to all peers every 2s.
// Monitors last_seen; calls on_node_failure when a peer has been silent 6s+.
// ─────────────────────────────────────────────────────────────────────────────
class HeartbeatService {
public:
    static constexpr int SEND_INTERVAL_MS    = 2000;
    static constexpr int TIMEOUT_MS          = 6000;
    static constexpr int MONITOR_INTERVAL_MS = 3000;

    using FailureCallback = std::function<void(int node_id)>;
    using RecoveryCallback = std::function<void(int node_id)>;

    HeartbeatService(int my_id, int my_port,
                     std::vector<NodeInfo> peers,
                     FailureCallback on_fail,
                     RecoveryCallback on_recover);
    ~HeartbeatService() { stop(); }

    void start();
    void stop();
    void mark_alive(int node_id);   // called on receipt of any message from peer

    bool is_alive(int node_id) const;
    std::vector<int> get_dead_nodes() const;

private:
    void sender_loop();
    void monitor_loop();

    int                    my_id_;
    int                    my_port_;
    std::vector<NodeInfo>  peers_;
    FailureCallback        on_failure_;
    RecoveryCallback       on_recovery_;

    mutable std::mutex         last_seen_mu_;
    std::unordered_map<int, int64_t> last_seen_;   // node_id → epoch_ms
    std::unordered_map<int, bool>    known_alive_;  // tracks transitions

    std::atomic<bool> running_{false};
    std::thread sender_thread_;
    std::thread monitor_thread_;
};
