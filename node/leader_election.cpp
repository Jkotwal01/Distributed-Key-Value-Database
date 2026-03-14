#include "leader_election.h"
#include "tcp_client.h"
#include <algorithm>
#include <chrono>

LeaderElection::LeaderElection(int my_id, std::vector<NodeInfo> all_nodes,
                               LeaderCallback on_leader_change)
    : my_id_(my_id), all_nodes_(std::move(all_nodes)),
      on_leader_change_(std::move(on_leader_change))
{
    std::lock_guard<std::mutex> lk(nodes_mu_);
    for (auto& n : all_nodes_) node_alive_[n.id] = true;
}

void LeaderElection::start() {
    running_ = true;
    // Give nodes a brief warm-up window, then auto-elect
    std::thread([this](){
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        trigger_election();
    }).detach();
}

void LeaderElection::stop() {
    running_ = false;
}

int LeaderElection::find_highest_alive_id() const {
    std::lock_guard<std::mutex> lk(nodes_mu_);
    int highest = my_id_;
    for (auto& [id, alive] : node_alive_) {
        if (alive && id > highest) highest = id;
    }
    return highest;
}

void LeaderElection::trigger_election() {
    if (!running_) return;
    if (election_in_progress_.exchange(true)) return; // already running

    int term = ++election_term_;
    got_higher_ack_ = false;
    LOG_INFO("Election triggered (term=" + std::to_string(term) + ", my_id=" + std::to_string(my_id_) + ")");

    // Send ELECTION to all nodes with higher id
    Message election_msg;
    election_msg.type    = MsgType::ELECTION;
    election_msg.node_id = my_id_;
    election_msg.term    = term;

    std::vector<NodeInfo> higher_nodes;
    {
        std::lock_guard<std::mutex> lk(nodes_mu_);
        for (auto& n : all_nodes_) {
            if (n.id > my_id_ && node_alive_[n.id]) higher_nodes.push_back(n);
        }
    }

    for (auto& n : higher_nodes) {
        auto resp = send_message(n.host, n.port, election_msg);
        if (resp && resp->type == MsgType::ACK) {
            got_higher_ack_ = true;
        }
    }

    if (!got_higher_ack_) {
        // No higher node replied — I become leader
        current_leader_id_ = my_id_;
        election_in_progress_ = false;
        LOG_INFO("I am the new LEADER (id=" + std::to_string(my_id_) + ")");
        broadcast_leader();
        if (on_leader_change_) on_leader_change_(my_id_);
    } else {
        // A higher node is alive — wait for LEADER message
        // Use a timeout thread in case higher node also fails
        std::thread([this, term](){
            std::this_thread::sleep_for(
                std::chrono::milliseconds(ELECTION_TIMEOUT_MS));
            if (election_in_progress_ && election_term_.load() == term) {
                // Still no LEADER received — re-trigger
                election_in_progress_ = false;
                trigger_election();
            }
        }).detach();
        election_in_progress_ = false;
    }
}

void LeaderElection::broadcast_leader() {
    Message leader_msg;
    leader_msg.type      = MsgType::LEADER;
    leader_msg.node_id   = my_id_;
    leader_msg.term      = election_term_.load();

    std::lock_guard<std::mutex> lk(nodes_mu_);
    for (auto& n : all_nodes_) {
        if (n.id != my_id_) {
            std::thread([n, leader_msg](){
                send_and_forget(n.host, n.port, leader_msg);
            }).detach();
        }
    }
}

std::optional<Message> LeaderElection::handle_message(const Message& msg) {
    if (msg.type == MsgType::ELECTION) {
        // Update node as alive
        {
            std::lock_guard<std::mutex> lk(nodes_mu_);
            node_alive_[msg.node_id] = true;
        }

        if (msg.node_id < my_id_) {
            // Reply ACK — I have a higher id, I'll take over
            Message ack;
            ack.type    = MsgType::ACK;
            ack.node_id = my_id_;
            ack.success = true;

            // Trigger my own election (detached — don't block the caller)
            std::thread([this](){ trigger_election(); }).detach();
            return ack;
        }
        return std::nullopt;
    }

    if (msg.type == MsgType::LEADER) {
        // Accept new leader announcement
        if (msg.node_id > current_leader_id_.load() ||
            msg.term > election_term_.load())
        {
            current_leader_id_ = msg.node_id;
            election_term_     = msg.term;
            election_in_progress_ = false;
            LOG_INFO("Accepted LEADER: node " + std::to_string(msg.node_id));
            {
                std::lock_guard<std::mutex> lk(nodes_mu_);
                node_alive_[msg.node_id] = true;
            }
            if (on_leader_change_) on_leader_change_(msg.node_id);
        }
        Message ack;
        ack.type    = MsgType::ACK;
        ack.node_id = my_id_;
        ack.success = true;
        return ack;
    }

    if (msg.type == MsgType::HEARTBEAT) {
        // Track node liveness for election decisions
        std::lock_guard<std::mutex> lk(nodes_mu_);
        node_alive_[msg.node_id] = true;
        return std::nullopt;
    }

    return std::nullopt;
}
