#include "ratehub/http_sse.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace ratehub {
namespace {

void close_fd(int& fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void set_nonblock(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

bool send_all(int fd, const char* data, std::size_t len) noexcept {
    std::size_t sent = 0;
    while (sent < len) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        const int pr = ::poll(&pfd, 1, 500);
        if (pr <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return false;
        }
#if defined(MSG_NOSIGNAL)
        const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, data + sent, len - sent, 0);
#endif
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool read_request(int fd, char* buf, std::size_t cap) noexcept {
    std::size_t got = 0;
    while (got + 1 < cap) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 500);
        if (pr <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return false;
        }
        const ssize_t n = ::recv(fd, buf + got, cap - 1 - got, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
        buf[got] = '\0';
        if (std::strstr(buf, "\r\n\r\n") != nullptr) {
            return true;
        }
    }
    return false;
}

void drop_if_closed(int& fd) noexcept {
    if (fd < 0) {
        return;
    }
    char tmp[32];
    const ssize_t n = ::recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        close_fd(fd);
    }
}

const char* request_path(char* buf) noexcept {
    char* sp1 = std::strchr(buf, ' ');
    if (sp1 == nullptr) {
        return nullptr;
    }
    *sp1 = '\0';
    const char* method = buf;
    char* path = sp1 + 1;
    char* sp2 = std::strchr(path, ' ');
    if (sp2 == nullptr) {
        return nullptr;
    }
    *sp2 = '\0';
    if (std::strcmp(method, "GET") != 0 && std::strcmp(method, "POST") != 0) {
        return nullptr;
    }
    return path;
}

bool is_post(const char* buf) noexcept {
    return std::strncmp(buf, "POST", 4) == 0;
}

bool send_reset_ok(int fd) noexcept {
    const char* body = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 8\r\n"
                       "Cache-Control: no-cache\r\nConnection: close\r\n\r\n{\"ok\":1}";
    return send_all(fd, body, std::strlen(body));
}

bool send_dashboard(int fd, const char* web_root) noexcept {
    char path[512];
    const int n = std::snprintf(path, sizeof(path), "%s/dashboard.html", web_root != nullptr ? web_root : "web");
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(path)) {
        return false;
    }
    const int file = ::open(path, O_RDONLY);
    if (file < 0) {
        const char* body = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        return send_all(fd, body, std::strlen(body));
    }
    struct stat st {};
    if (::fstat(file, &st) != 0 || st.st_size < 0) {
        ::close(file);
        return false;
    }
    char header[128];
    const int hlen =
        std::snprintf(header, sizeof(header),
                      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lld\r\n"
                      "Cache-Control: no-cache\r\nConnection: close\r\n\r\n",
                      static_cast<long long>(st.st_size));
    if (hlen <= 0 || !send_all(fd, header, static_cast<std::size_t>(hlen))) {
        ::close(file);
        return false;
    }
    char chunk[4096];
    for (;;) {
        const ssize_t r = ::read(file, chunk, sizeof(chunk));
        if (r < 0) {
            ::close(file);
            return false;
        }
        if (r == 0) {
            break;
        }
        if (!send_all(fd, chunk, static_cast<std::size_t>(r))) {
            ::close(file);
            return false;
        }
    }
    ::close(file);
    return true;
}

bool send_sse_headers(int fd) noexcept {
    const char* headers =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
        "Connection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
    return send_all(fd, headers, std::strlen(headers));
}

bool send_sse_data(int fd, const char* payload, std::size_t len) noexcept {
    char line[8192];
    if (len >= 8000) {
        return false;
    }
    const int n = std::snprintf(line, sizeof(line), "data: %.*s\n\n", static_cast<int>(len), payload);
    if (n <= 0) {
        return false;
    }
    return send_all(fd, line, static_cast<std::size_t>(n));
}

int accept_client(int listen_fd) noexcept {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    const int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
    if (fd < 0) {
        return -1;
    }
#if defined(SO_NOSIGPIPE)
    const int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    set_nonblock(fd);
    return fd;
}

}  // namespace

bool http_sse_listen(HttpSseServer& srv, std::uint16_t port) noexcept {
    http_sse_close(srv);
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    const int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    if (::listen(fd, 8) != 0) {
        ::close(fd);
        return false;
    }
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        ::close(fd);
        return false;
    }
    srv.listen_fd = fd;
    srv.port = ntohs(bound.sin_port);
    return true;
}

void http_sse_close(HttpSseServer& srv) noexcept {
    close_fd(srv.listen_fd);
    srv.port = 0;
}

bool http_sse_serve_once(HttpSseServer& srv, const char* web_root, const char* sse_payload,
                         std::size_t sse_len) noexcept {
    if (srv.listen_fd < 0) {
        return false;
    }
    const int client = accept_client(srv.listen_fd);
    if (client < 0) {
        return false;
    }
    char req[1024];
    if (!read_request(client, req, sizeof(req))) {
        ::close(client);
        return false;
    }
    const char* path = request_path(req);
    bool ok = false;
    if (path != nullptr && std::strcmp(path, "/events") == 0 && !is_post(req)) {
        ok = send_sse_headers(client) && send_sse_data(client, sse_payload, sse_len);
    } else if (path != nullptr && (std::strcmp(path, "/") == 0 || std::strcmp(path, "/dashboard.html") == 0) &&
               !is_post(req)) {
        ok = send_dashboard(client, web_root);
    } else if (path != nullptr && std::strcmp(path, "/reset") == 0 && is_post(req)) {
        ok = send_reset_ok(client);
    } else {
        const char* body = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(client, body, std::strlen(body));
    }
    ::close(client);
    return ok;
}

bool http_sse_pump(HttpSseServer& srv, int& client_fd, const char* web_root, const char* json, std::size_t json_len,
                   int timeout_ms, bool* reset_out) noexcept {
    if (srv.listen_fd < 0) {
        return false;
    }
    pollfd fds[2]{};
    fds[0].fd = srv.listen_fd;
    fds[0].events = POLLIN;
    int nfds = 1;
    if (client_fd >= 0) {
        fds[1].fd = client_fd;
        fds[1].events = POLLIN;
        nfds = 2;
    }
    const int pr = ::poll(fds, static_cast<nfds_t>(nfds), timeout_ms);
    if (pr < 0) {
        return errno == EINTR;
    }
    if (nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
        drop_if_closed(client_fd);
    }
    if ((fds[0].revents & POLLIN) != 0) {
        const int incoming = accept_client(srv.listen_fd);
        if (incoming >= 0) {
            char req[1024];
            if (!read_request(incoming, req, sizeof(req))) {
                ::close(incoming);
            } else {
                const char* path = request_path(req);
                if (path != nullptr && (std::strcmp(path, "/") == 0 || std::strcmp(path, "/dashboard.html") == 0) &&
                    !is_post(req)) {
                    send_dashboard(incoming, web_root);
                    ::close(incoming);
                } else if (path != nullptr && std::strcmp(path, "/events") == 0 && !is_post(req)) {
                    if (client_fd >= 0) {
                        ::close(client_fd);
                        client_fd = -1;
                    }
                    if (!send_sse_headers(incoming)) {
                        ::close(incoming);
                    } else {
                        client_fd = incoming;
                    }
                } else if (path != nullptr && std::strcmp(path, "/reset") == 0 && is_post(req)) {
                    send_reset_ok(incoming);
                    ::close(incoming);
                    if (reset_out != nullptr) {
                        *reset_out = true;
                    }
                } else {
                    const char* body = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    send_all(incoming, body, std::strlen(body));
                    ::close(incoming);
                }
            }
        }
    }
    if (client_fd >= 0 && json != nullptr && json_len > 0) {
        if (!send_sse_data(client_fd, json, json_len)) {
            close_fd(client_fd);
        }
    }
    return true;
}

}  // namespace ratehub
