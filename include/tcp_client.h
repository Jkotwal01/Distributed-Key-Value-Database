#pragma once
// tcp_client.h — Header-only TCP client helpers for inter-node communication.
// Opens a fresh connection per call (stateless), sends JSON + '\n', reads reply.
// Fire-and-forget variant available for async replication.

#include "utils.h"
#include "tcp_server.h"  // for SocketFd and platform macros
#include <optional>
#include <string>
#include <cstring>
#include <stdexcept>

#ifdef PLATFORM_WINDOWS
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
#endif

inline SocketFd make_connection(const std::string& host, int port, int timeout_ms = 2000) {
    SocketFd fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    // Set receive timeout
#ifdef PLATFORM_WINDOWS
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));

    // Resolve hostname
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) { CLOSE_SOCK(fd); return INVALID_SOCK; }
    addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSE_SOCK(fd);
        return INVALID_SOCK;
    }
    return fd;
}

// ─── send_message ─────────────────────────────────────────────────────────────
// Send a Message to host:port, wait for reply. Returns nullopt on error.
inline std::optional<Message> send_message(const std::string& host, int port,
                                           const Message& msg)
{
    SocketFd fd = make_connection(host, port);
    if (fd == INVALID_SOCK) {
        LOG_WARN("send_message: cannot connect to " + host + ":" + std::to_string(port));
        return std::nullopt;
    }

    std::string data = serialize_message(msg);
    size_t total = 0;
    while (total < data.size()) {
        int sent = send(fd, data.c_str() + total,
                        static_cast<int>(data.size() - total), 0);
        if (sent <= 0) { CLOSE_SOCK(fd); return std::nullopt; }
        total += static_cast<size_t>(sent);
    }

    // Read newline-delimited reply
    std::string reply;
    char c;
    while (true) {
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        reply += c;
    }
    CLOSE_SOCK(fd);

    if (reply.empty()) return std::nullopt;
    return deserialize_message(reply);
}

// ─── send_and_forget ──────────────────────────────────────────────────────────
// Fire-and-forget: send message, don't wait for reply. Used for async replication.
inline void send_and_forget(const std::string& host, int port, const Message& msg) {
    SocketFd fd = make_connection(host, port, 1000);
    if (fd == INVALID_SOCK) return;
    std::string data = serialize_message(msg);
    send(fd, data.c_str(), static_cast<int>(data.size()), 0);
    CLOSE_SOCK(fd);
}
