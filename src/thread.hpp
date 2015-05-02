#pragma once

#include <thread>

namespace demo
{

    void thread_affinity(const std::vector<int>& cpus)
    {
        cpu_set_t expected;
        CPU_ZERO(&expected);
        for (auto cpu : cpus) {
            CPU_SET(cpu, &expected);
        }
        auto self = ::pthread_self();
        if (::pthread_setaffinity_np(self, sizeof(expected), &expected) != 0) {
            throw std::runtime_error("pthread-affinity-error");
        }
        cpu_set_t actual;
        if (::pthread_getaffinity_np(self, sizeof(actual), &actual) != 0) {
            throw std::runtime_error("pthread-affinity-error");
        }
        if (!CPU_EQUAL(&expected, &actual)) {
            throw std::runtime_error("pthread-affinity-error");
        }
    }

}
