#pragma once

#include <thread>

namespace janus::common::utils {

void pin_self_to_core(int core);

void pin_thread_to_core(std::thread& t, int core);

}  // namespace janus::common::utils
