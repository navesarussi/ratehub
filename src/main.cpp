#include "ratehub/roles.hpp"

#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "ingest") == 0) {
        return ratehub::run_ingest(argv[2], argv[3]);
    }
    if (argc == 3 && std::strcmp(argv[1], "compute") == 0) {
        return ratehub::run_compute(argv[2]);
    }
    if (argc == 4 && std::strcmp(argv[1], "publish") == 0) {
        return ratehub::run_publish(argv[2], argv[3]);
    }
    if (argc == 4 && std::strcmp(argv[1], "run") == 0) {
        return ratehub::run_supervisor(argv[0], argv[2], argv[3]);
    }
    std::cerr << "usage: ratehub run <replay> <out>\n";
    return 2;
}
