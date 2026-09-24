#pragma once

// Minimal HTTP/1.1 server for the observer. Binds 127.0.0.1. The hot path
// never calls these functions.

#include <cstddef>
#include <cstdint>

namespace ratehub {

struct HttpSseServer {
    int listen_fd = -1;
    std::uint16_t port = 0;
};

bool http_sse_listen(HttpSseServer& srv, std::uint16_t port) noexcept;
void http_sse_close(HttpSseServer& srv) noexcept;
// Accept one client. /events sends one SSE data line from sse_payload and
// returns true. / or /dashboard.html serves web_root/dashboard.html.
bool http_sse_serve_once(HttpSseServer& srv, const char* web_root, const char* sse_payload,
                         std::size_t sse_len) noexcept;
// Accept HTML (send and close) or hold one SSE client in client_fd. Sends one
// event when client_fd is already an SSE socket. timeout_ms is for accept.
bool http_sse_pump(HttpSseServer& srv, int& client_fd, const char* web_root, const char* json, std::size_t json_len,
                   int timeout_ms, bool* reset_out = nullptr) noexcept;

}  // namespace ratehub
