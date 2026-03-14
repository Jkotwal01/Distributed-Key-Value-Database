#pragma once
#include "utils.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>

// ─── LeaderElection (Bully Algorithm) ────────────────────────────────────────
// The node with the highest ID that is alive becomes leader.
//
// Election flow:
//  1. Trigger: current leader silence or startup
//  2. Send ELECTION to all nodes with id > mine
//  3. If no higher-id node replies within ELECTION_TIMEOUT_MS → I'm leader
//     else wait; higher node will send LEADER broadcast
//  4. On receiving LEADER → update current_leader_id
// ─────────────────────────────────────────────────────────────────────────────
class LeaderElection {
public:
    static constexpr int ELECTION_TIMEOUT_MS = 5000;

    using LeaderCallback = std::function<void(int new_leader_id)>;

    LeaderElection(int my_id, std::vector<NodeInfo> all_nodes,
                   LeaderCallback on_leader_change);
    ~LeaderElection() { stop(); }

    void start();
    void stop();
    void trigger_election();

    // Returns nullopt for no-reply (fire-and-forget) or an ACK/LEADER message
    std::optional<Message> handle_message(const Message& msg);

    int  get_leader() const { return current_leader_id_.load(); }
    bool am_i_leader() const { return current_leader_id_.load() == my_id_; }

private:
    void broadcast_leader();
    int  find_highest_alive_id() const;

    int                   my_id_;
    std::vector<NodeInfo> all_nodes_;
    LeaderCallback        on_leader_change_;

    std::atomic<int>  current_leader_id_{-1};
    std::atomic<bool> election_in_progress_{false};
    std::atomic<int>  election_term_{0};
    std::atomic<bool> got_higher_ack_{false};
    std::atomic<bool> running_{false};

    mutable std::mutex nodes_mu_;
    std::unordered_map<int, bool> node_alive_; // id → alive
};
