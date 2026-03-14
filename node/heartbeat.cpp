#include "heartbeat.h"
#include "tcp_client.h"
#include <algorithm>

HeartbeatService::HeartbeatService(int my_id, int my_port,
                                   std::vector<NodeInfo> peers,
                                   FailureCallback on_fail,
                                   RecoveryCallback on_recover)
    : my_id_(my_id), my_port_(my_port), peers_(std::move(peers)),
      on_failure_(std::move(on_fail)), on_recovery_(std::move(on_recover))
{
    std::lock_guard<std::mutex> lk(last_seen_mu_);
    int64_t now = now_ms();
    for (auto& p : peers_) {
        last_seen_[p.id]   = now;     // assume alive at start
        known_alive_[p.id] = true;
    }
}

void HeartbeatService::start() {
    running_ = true;
    sender_thread_  = std::thread([this]{ sender_loop();  });
    monitor_thread_ = std::thread([this]{ monitor_loop(); });
}

void HeartbeatService::stop() {
    running_ = false;
    if (sender_thread_.joinable())  sender_thread_.join();
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

void HeartbeatService::mark_alive(int node_id) {
    std::lock_guard<std::mutex> lk(last_seen_mu_);
    last_seen_[node_id] = now_ms();
    // Node came back up — fire recovery callback if previously dead
    auto it = known_alive_.find(node_id);
    if (it != known_alive_.end() && !it->second) {
        it->second = true;
        if (on_recovery_) on_recovery_(node_id);
    } else {
        known_alive_[node_id] = true;
    }
}

bool HeartbeatService::is_alive(int node_id) const {
    std::lock_guard<std::mutex> lk(last_seen_mu_);
    auto it = last_seen_.find(node_id);
    if (it == last_seen_.end()) return false;
    return (now_ms() - it->second) < TIMEOUT_MS;
}

std::vector<int> HeartbeatService::get_dead_nodes() const {
    std::vector<int> dead;
    std::lock_guard<std::mutex> lk(last_seen_mu_);
    int64_t now = now_ms();
    for (auto& [id, ts] : last_seen_) {
        if (now - ts >= TIMEOUT_MS) dead.push_back(id);
    }
    return dead;
}

void HeartbeatService::sender_loop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(SEND_INTERVAL_MS));
        if (!running_) break;

        Message hb;
        hb.type      = MsgType::HEARTBEAT;
        hb.node_id   = my_id_;
        hb.timestamp = now_ms();

        for (auto& peer : peers_) {
            // Fire-and-forget — don't block sender loop on slow peers
            std::thread([peer, hb](){
                send_and_forget(peer.host, peer.port, hb);
            }).detach();
        }
    }
}

void HeartbeatService::monitor_loop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(MONITOR_INTERVAL_MS));
        if (!running_) break;

        int64_t now = now_ms();
        std::lock_guard<std::mutex> lk(last_seen_mu_);
        for (auto& [id, ts] : last_seen_) {
            bool currently_alive = (now - ts) < TIMEOUT_MS;
            auto& ka = known_alive_[id];
            if (ka && !currently_alive) {
                // Node just died
                ka = false;
                LOG_WARN("Node " + std::to_string(id) + " detected as FAILED (last seen "
                         + std::to_string((now - ts)/1000) + "s ago)");
                if (on_failure_) on_failure_(id);
            }
        }
    }
}
