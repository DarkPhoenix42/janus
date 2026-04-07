#include "include/common/utils.h"

#include <pthread.h>

#include <thread>

namespace janus::common::utils {

void pin_self_to_core(int core) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#else
    (void)core;  // no-op on non-Linux
#endif
}

void pin_thread_to_core(std::thread& t, int core) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpuset), &cpuset);
#else
    (void)t;
    (void)core;  // no-op on non-Linux
#endif
}

}  // namespace janus::common::utils
