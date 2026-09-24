#include <cstdio>

int test_frame();
int test_fleet();
int test_publish();
int test_system();
int test_pace();

int main() {
    const int failed = test_frame() + test_fleet() + test_publish() + test_pace() + test_system();
    if (failed != 0) {
        std::fprintf(stderr, "%d failed\n", failed);
        return 1;
    }
    std::puts("ALL PASS");
    return 0;
}
