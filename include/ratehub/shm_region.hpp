#pragma once

#include "ratehub/layout.hpp"

#include <string>

namespace ratehub {

enum class ShmStatus {
    Ok,
    IoError,
    SchemaMismatch,
};

// owner true only for the supervisor that created the mapping.
// close() unlinks the name only then. Children map, use, and detach.
struct ShmMapping {
    Layout* layout = nullptr;
    int fd = -1;
    bool owner = false;
    std::string name;
};

ShmStatus shm_create(const char* name, ShmMapping& out);
ShmStatus shm_open_existing(const char* name, ShmMapping& out);
void shm_close(ShmMapping& mapping);

}  // namespace ratehub
