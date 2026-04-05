#include "include/common/utils.h"

#include <thread>

namespace janus::common::utils {

void pin_thread(int cpu) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#else
    (void)cpu;  // no-op on non-Linux
#endif
}

}  // namespace janus::common::utils
