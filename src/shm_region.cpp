#include "ratehub/shm_region.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <new>

namespace ratehub {
namespace {

ShmStatus map_fd(int fd, bool construct, ShmMapping& out) {
    if (construct && ftruncate(fd, static_cast<off_t>(sizeof(Layout))) != 0) {
        return ShmStatus::IoError;
    }
    void* mem = mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        return ShmStatus::IoError;
    }
    auto* layout = static_cast<Layout*>(mem);
    if (construct) {
        new (layout) Layout;
    } else if (layout->control.magic != kShmMagic || layout->control.schema != kSchemaVersion) {
        munmap(mem, sizeof(Layout));
        return ShmStatus::SchemaMismatch;
    }
    out.layout = layout;
    out.fd = fd;
    return ShmStatus::Ok;
}

}  // namespace

ShmStatus shm_create(const char* name, ShmMapping& out) {
    shm_unlink(name);
    const int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        return ShmStatus::IoError;
    }
    const ShmStatus status = map_fd(fd, true, out);
    if (status != ShmStatus::Ok) {
        close(fd);
        shm_unlink(name);
        return status;
    }
    out.owner = true;
    out.name = name;
    return ShmStatus::Ok;
}

ShmStatus shm_open_existing(const char* name, ShmMapping& out) {
    const int fd = shm_open(name, O_RDWR, 0600);
    if (fd < 0) {
        return ShmStatus::IoError;
    }
    const ShmStatus status = map_fd(fd, false, out);
    if (status != ShmStatus::Ok) {
        close(fd);
        return status;
    }
    out.owner = false;
    out.name = name;
    return ShmStatus::Ok;
}

void shm_close(ShmMapping& mapping) {
    if (mapping.layout != nullptr) {
        munmap(mapping.layout, sizeof(Layout));
        mapping.layout = nullptr;
    }
    if (mapping.fd >= 0) {
        close(mapping.fd);
        mapping.fd = -1;
    }
    if (mapping.owner && !mapping.name.empty()) {
        shm_unlink(mapping.name.c_str());
    }
    mapping.owner = false;
    mapping.name.clear();
}

}  // namespace ratehub
