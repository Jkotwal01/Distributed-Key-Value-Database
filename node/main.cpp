#include "utils.h"
#include "tcp_server.h"
#include "storage_engine.h"
#include "hash_ring.h"
#include "heartbeat.h"
#include "leader_election.h"
#include "replication.h"
#include "coordinator.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <csignal>
#include <atomic>

// ─── Global stop flag ─────────────────────────────────────────────────────────
static std::atomic<bool> g_stop{false};
static void sig_handler(int) { g_stop = true; }

// ─── CLI parsing ─────────────────────────────────────────────────────────────
struct Config {
    int               node_id{1};
    int               port{8001};
    std::string       host{"localhost"};
    std::string       wal_path{"./data/wal.log"};
    std::vector<NodeInfo> peers;
};

static NodeInfo parse_peer(const std::string& s, int id_hint) {
    // Format: "host:port"
    auto colon = s.rfind(':');
    if (colon == std::string::npos) throw std::invalid_argument("Bad peer: " + s);
    NodeInfo n;
    n.host = s.substr(0, colon);
    n.port = std::stoi(s.substr(colon + 1));
    n.id   = id_hint;  // will be filled in upon first HEARTBEAT contact
    return n;
}

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--id"   && i+1 < argc) { cfg.node_id = std::stoi(argv[++i]); }
        else if (arg == "--port"  && i+1 < argc) { cfg.port    = std::stoi(argv[++i]); }
        else if (arg == "--host"  && i+1 < argc) { cfg.host    = argv[++i]; }
        else if (arg == "--wal"   && i+1 < argc) { cfg.wal_path = argv[++i]; }
        else if (arg == "--peers" && i+1 < argc) {
            // comma-separated list: "host:port,host:port,..."
            // Peer IDs assigned sequentially relative to my id (approximation)
            std::istringstream ss(argv[++i]);
            std::string tok;
            int peer_seq = 1;
            while (std::getline(ss, tok, ',')) {
                if (tok.empty()) continue;
                // Assign peer_id = peer_seq, skip my own id
                if (peer_seq == cfg.node_id) ++peer_seq;
                cfg.peers.push_back(parse_peer(tok, peer_seq++));
            }
        }
    }
    return cfg;
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    Logger::instance().set_node_id(cfg.node_id);
    LOG_INFO("Starting distributed-db node id=" + std::to_string(cfg.node_id)
             + " on port " + std::to_string(cfg.port));

    // ── WAL path per node (avoid collisions in same dir) ────────────────────
    std::string wal = "./data/node" + std::to_string(cfg.node_id) + "_wal.log";
    if (cfg.wal_path != "./data/wal.log") wal = cfg.wal_path;

    // ── Instantiate all services ─────────────────────────────────────────────
    StorageEngine storage(wal);

    NodeInfo self_info;
    self_info.id   = cfg.node_id;
    self_info.host = cfg.host;
    self_info.port = cfg.port;

    HashRing ring;
    ring.add_node(self_info);
    for (auto& p : cfg.peers) ring.add_node(p);

    ReplicationManager replication(cfg.node_id, storage);

    std::vector<NodeInfo> all_nodes = {self_info};
    for (auto& p : cfg.peers) all_nodes.push_back(p);

    LeaderElection election(cfg.node_id, all_nodes, [](int leader_id){
        LOG_INFO("=== New cluster leader: Node " + std::to_string(leader_id) + " ===");
    });

    Coordinator coordinator(cfg.node_id, self_info,
                             ring, storage, replication, election);

    HeartbeatService heartbeat(
        cfg.node_id, cfg.port, cfg.peers,
        // on_failure
        [&coordinator](int id){ coordinator.on_node_failure(id); },
        // on_recovery
        [&coordinator, &replication, &ring](int id){
            coordinator.on_node_recovery(id);
            // Sync data from the recovered node's perspective is handled by that node
        }
    );

    // ── TCP server ────────────────────────────────────────────────────────────
    TcpServer server;
    server.set_handler([&coordinator, &heartbeat](const Message& msg) -> Message {
        // Update heartbeat on any received message
        heartbeat.mark_alive(msg.node_id);
        return coordinator.handle(msg);
    });

    // ── Start services ────────────────────────────────────────────────────────
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    heartbeat.start();
    election.start();

    // If peers exist, try to sync from the first available peer
    if (!cfg.peers.empty()) {
        std::thread([&replication, peers = cfg.peers](){
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            for (auto& p : peers) {
                LOG_INFO("Attempting initial sync from peer node " + std::to_string(p.id));
                replication.sync_from_peer(p);
                break;  // sync from first reachable peer
            }
        }).detach();
    }

    LOG_INFO("Node ready. Press Ctrl+C to stop.");
    server.start(cfg.port);   // blocking

    // Cleanup on exit
    heartbeat.stop();
    election.stop();
    LOG_INFO("Node " + std::to_string(cfg.node_id) + " shut down cleanly.");
    return 0;
}
