#include "tcp_server.h"
#include "utils.h"
#include <stdexcept>
#include <cstring>
#include <sstream>

#ifdef PLATFORM_WINDOWS
  #pragma comment(lib, "ws2_32.lib")
namespace {
  struct WsaInit {
      WsaInit()  { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
      ~WsaInit() { WSACleanup(); }
  };
  static WsaInit wsa_init__;
}
#endif

static std::string recv_line(SocketFd fd) {
    std::string buf;
    char c;
    while (true) {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        buf += c;
    }
    return buf;
}

static void send_all(SocketFd fd, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        int sent = send(fd, data.c_str() + total,
                        static_cast<int>(data.size() - total), 0);
        if (sent <= 0) break;
        total += static_cast<size_t>(sent);
    }
}

void TcpServer::handle_connection(SocketFd client_fd) {
    try {
        std::string line = recv_line(client_fd);
        if (!line.empty() && handler_) {
            Message req  = deserialize_message(line);
            Message resp = handler_(req);
            std::string out = serialize_message(resp);
            send_all(client_fd, out);
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Connection handler error: ") + e.what());
    }
    CLOSE_SOCK(client_fd);
}

void TcpServer::start(int port) {
    running_ = true;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == INVALID_SOCK)
        throw std::runtime_error("socket() failed");

    // Allow port reuse for fast restart
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed on port " + std::to_string(port));

    if (listen(server_fd_, 128) < 0)
        throw std::runtime_error("listen() failed");

    LOG_INFO("TCP server listening on port " + std::to_string(port));

    while (running_) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        SocketFd    client_fd  = accept(server_fd_,
                                        reinterpret_cast<sockaddr*>(&client_addr),
                                        &client_len);
        if (client_fd == INVALID_SOCK) {
            if (running_) LOG_WARN("accept() returned invalid socket");
            break;
        }
        // Detached thread per connection (fire-and-forget)
        std::thread t([this, client_fd]() { handle_connection(client_fd); });
        t.detach();
    }
}

void TcpServer::stop() {
    running_ = false;
    if (server_fd_ != INVALID_SOCK) {
        CLOSE_SOCK(server_fd_);
        server_fd_ = INVALID_SOCK;
    }
}
