#pragma once
#include "utils.h"
#include <functional>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <memory>

#ifdef PLATFORM_WINDOWS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using SocketFd = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
  using SocketFd = int;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCK(s) close(s)
#endif

// ─── TcpServer ────────────────────────────────────────────────────────────────
// Thread-per-connection TCP server. Each connection receives a newline-delimited
// JSON message, dispatches to the handler callback, and writes back the response.
// ─────────────────────────────────────────────────────────────────────────────
class TcpServer {
public:
    // handler: receives a Message, returns an optional reply Message
    using Handler = std::function<Message(const Message&)>;

    TcpServer() = default;
    ~TcpServer() { stop(); }

    void set_handler(Handler h) { handler_ = std::move(h); }
    void start(int port);   // blocking — runs accept loop
    void stop();

private:
    void accept_loop(int port);
    void handle_connection(SocketFd client_fd);

    Handler              handler_;
    std::atomic<bool>    running_{false};
    SocketFd             server_fd_{INVALID_SOCK};
    std::vector<std::thread> conn_threads_;
    std::mutex           threads_mu_;
};
