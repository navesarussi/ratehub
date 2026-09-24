#include "ratehub/http_sse.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

int test_http_sse() {
    ratehub::HttpSseServer srv;
    if (!ratehub::http_sse_listen(srv, 0)) {
        std::fprintf(stderr, "FAIL listen\n");
        return 1;
    }
    const std::uint16_t port = srv.port;
    char payload[] = "{\"ok\":1}";
    std::atomic<int> served{0};
    std::thread server([&] {
        served.store(ratehub::http_sse_serve_once(srv, "web", payload, std::strlen(payload)) ? 1 : 0);
    });
    int fd = -1;
    for (int i = 0; i < 50 && fd < 0; ++i) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            fd = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    int failed = 0;
    if (fd < 0) {
        std::fprintf(stderr, "FAIL connect\n");
        ratehub::http_sse_close(srv);
        server.join();
        return 1;
    }
    const char req[] = "GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (::send(fd, req, sizeof(req) - 1, 0) < 0) {
        std::fprintf(stderr, "FAIL send\n");
        failed = 1;
    } else {
        std::string got;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        char buf[1024];
        while (got.find("data: {") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            got.append(buf, static_cast<std::size_t>(n));
        }
        if (got.find("data: {") == std::string::npos) {
            std::fprintf(stderr, "FAIL sse body [%s]\n", got.c_str());
            failed = 1;
        }
    }
    ::close(fd);
    server.join();
    ratehub::http_sse_close(srv);
    if (failed != 0) {
        return 1;
    }
    if (served.load() == 0) {
        std::fprintf(stderr, "FAIL serve_once\n");
        return 1;
    }

    ratehub::HttpSseServer reset_srv;
    if (!ratehub::http_sse_listen(reset_srv, 0)) {
        std::fprintf(stderr, "FAIL reset listen\n");
        return 1;
    }
    std::atomic<int> reset_served{0};
    std::thread reset_server([&] {
        reset_served.store(ratehub::http_sse_serve_once(reset_srv, "web", payload, std::strlen(payload)) ? 1 : 0);
    });
    int rfd = -1;
    for (int i = 0; i < 50 && rfd < 0; ++i) {
        rfd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(reset_srv.port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(rfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(rfd);
            rfd = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (rfd < 0) {
        std::fprintf(stderr, "FAIL reset connect\n");
        ratehub::http_sse_close(reset_srv);
        reset_server.join();
        return 1;
    }
    const char reset_req[] = "POST /reset HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
    if (::send(rfd, reset_req, sizeof(reset_req) - 1, 0) < 0) {
        std::fprintf(stderr, "FAIL reset send\n");
        ::close(rfd);
        ratehub::http_sse_close(reset_srv);
        reset_server.join();
        return 1;
    }
    std::string reset_got;
    const auto reset_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    char rbuf[1024];
    while (reset_got.find("\"ok\":1") == std::string::npos && std::chrono::steady_clock::now() < reset_deadline) {
        const ssize_t n = ::recv(rfd, rbuf, sizeof(rbuf), 0);
        if (n <= 0) {
            break;
        }
        reset_got.append(rbuf, static_cast<std::size_t>(n));
    }
    ::close(rfd);
    reset_server.join();
    ratehub::http_sse_close(reset_srv);
    if (reset_got.find("\"ok\":1") == std::string::npos || reset_served.load() == 0) {
        std::fprintf(stderr, "FAIL reset body [%s]\n", reset_got.c_str());
        return 1;
    }
    return 0;
}
