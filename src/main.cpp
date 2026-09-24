#include "ratehub/roles.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

std::string web_root_from_exe(const char* argv0) {
    std::string s = argv0 != nullptr ? argv0 : "";
    const auto slash = s.find_last_of('/');
    if (slash == std::string::npos) {
        return "web";
    }
    return s.substr(0, slash) + "/web";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "ingest") == 0) {
        return ratehub::run_ingest(argv[2], argv[3]);
    }
    if (argc == 3 && std::strcmp(argv[1], "compute") == 0) {
        return ratehub::run_compute(argv[2]);
    }
    if (argc == 4 && std::strcmp(argv[1], "publish") == 0) {
        return ratehub::run_publish(argv[2], argv[3]);
    }
    if (argc >= 4 && std::strcmp(argv[1], "observe") == 0) {
        const auto port = static_cast<std::uint16_t>(std::atoi(argv[3]));
        const std::uint32_t interval = argc >= 5 ? static_cast<std::uint32_t>(std::atoi(argv[4])) : 100u;
        const std::string web = web_root_from_exe(argv[0]);
        return ratehub::run_observe(argv[2], port, interval, web.c_str());
    }
    if (argc >= 4 && std::strcmp(argv[1], "run") == 0) {
        std::uint16_t observe_port = 0;
        if (argc >= 6 && std::strcmp(argv[4], "--observe-port") == 0) {
            observe_port = static_cast<std::uint16_t>(std::atoi(argv[5]));
        }
        return ratehub::run_supervisor(argv[0], argv[2], argv[3], observe_port);
    }
    std::cerr << "usage: ratehub run <replay> <out> [--observe-port N]\n"
              << "       ratehub observe <shm> <port> [interval_ms]\n";
    return 2;
}
