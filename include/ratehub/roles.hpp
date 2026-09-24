#pragma once

// Process entry points. The executable dispatches on argv.
// run_supervisor creates the mapping and spawns the other three roles
// from `exe`, which must be this same program.

namespace ratehub {

int run_ingest(const char* shm_name, const char* replay_path);
int run_compute(const char* shm_name);
int run_publish(const char* shm_name, const char* out_path);
int run_supervisor(const char* exe, const char* replay_path, const char* out_path);

}  // namespace ratehub
