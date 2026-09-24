#include "ratehub/frame.hpp"
#include "ratehub/roles.hpp"
#include "ratehub/shm_region.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int g_failed = 0;

void expect(bool cond, const char* name) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++g_failed;
    }
}

std::string write_replay(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    ratehub::Record record;
    record.source_id = 1;
    record.pos_x = 1000;
    record.vel_x = 2000;
    for (std::uint32_t seq = 1; seq <= 2; ++seq) {
        record.sequence = seq;
        std::uint8_t frame[ratehub::kFrameBytes];
        expect(ratehub::encode_frame(record, frame), "encode replay frame");
        out.write(reinterpret_cast<char*>(frame), static_cast<std::streamsize>(ratehub::kFrameBytes));
    }
    return path;
}

}  // namespace

int test_system() {
    setenv("RATEHUB_PACE", "0", 1);
    const std::string suffix = std::to_string(static_cast<long long>(::getpid()));
    const std::string shm = "/rh-schema-" + suffix;
    ratehub::ShmMapping created;
    expect(ratehub::shm_create(shm.c_str(), created) == ratehub::ShmStatus::Ok, "shm create");
    created.layout->control.schema = 99;
    ratehub::ShmMapping opened;
    expect(ratehub::shm_open_existing(shm.c_str(), opened) == ratehub::ShmStatus::SchemaMismatch, "schema mismatch");
    ratehub::shm_close(created);

    const std::string replay = "/tmp/ratehub-replay-" + suffix;
    const std::string snapshot = "/tmp/ratehub-out-" + suffix;
    write_replay(replay);
    unsetenv("RATEHUB_CRASH");
    expect(ratehub::run_supervisor(RATEHUB_BIN, replay.c_str(), snapshot.c_str()) == 0, "supervisor run");
    std::ifstream in(snapshot);
    std::string line;
    std::getline(in, line);
    expect(line == "generation 1", "generation 1");
    std::getline(in, line);
    expect(line == "1 1020 0 2", "two steps land at 1020 mm");

    setenv("RATEHUB_CRASH", "once", 1);
    const std::string crashed = snapshot + ".crash";
    expect(ratehub::run_supervisor(RATEHUB_BIN, replay.c_str(), crashed.c_str()) == 0, "restart once");
    std::ifstream crash_in(crashed);
    std::getline(crash_in, line);
    expect(line == "generation 2", "restart bumps generation");
    unsetenv("RATEHUB_CRASH");

    setenv("RATEHUB_CRASH", "always", 1);
    const std::string failed = snapshot + ".fail";
    expect(ratehub::run_supervisor(RATEHUB_BIN, replay.c_str(), failed.c_str()) == 1, "restart limit");
    unsetenv("RATEHUB_CRASH");
    return g_failed;
}
