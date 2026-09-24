#include "ratehub/roles.hpp"

#include "ratehub/shm_region.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <string>

extern char** environ;

namespace ratehub {
namespace {

constexpr int kMaxComputeAttempts = 3;

pid_t spawn_child(const char* exe, const char* role, const char* shm, const char* extra) {
    char* argv[5];
    argv[0] = const_cast<char*>(exe);
    argv[1] = const_cast<char*>(role);
    argv[2] = const_cast<char*>(shm);
    int argc = 3;
    if (extra != nullptr) {
        argv[argc++] = const_cast<char*>(extra);
    }
    argv[argc] = nullptr;
    pid_t pid = 0;
    if (posix_spawn(&pid, exe, nullptr, nullptr, argv, environ) != 0) {
        return -1;
    }
    return pid;
}

int wait_child(pid_t pid) {
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

}  // namespace

int run_supervisor(const char* exe, const char* replay_path, const char* out_path) {
    const std::string name = "/rh-" + std::to_string(static_cast<long long>(getpid()));
    ShmMapping mapping;
    if (shm_create(name.c_str(), mapping) != ShmStatus::Ok) {
        return 1;
    }
    const pid_t ingest = spawn_child(exe, "ingest", name.c_str(), replay_path);
    const pid_t publish = spawn_child(exe, "publish", name.c_str(), out_path);
    bool compute_ok = false;
    for (int attempt = 1; attempt <= kMaxComputeAttempts; ++attempt) {
        const pid_t compute = spawn_child(exe, "compute", name.c_str(), nullptr);
        const int status = wait_child(compute);
        if (status == 0) {
            compute_ok = true;
            break;
        }
        if (attempt == kMaxComputeAttempts) {
            break;
        }
        mapping.layout->control.generation.fetch_add(1, std::memory_order_release);
    }
    mapping.layout->control.shutdown.store(1, std::memory_order_release);
    const int ingest_status = wait_child(ingest);
    const int publish_status = wait_child(publish);
    shm_close(mapping);
    if (!compute_ok || ingest_status != 0 || publish_status != 0) {
        return 1;
    }
    return 0;
}

}  // namespace ratehub
