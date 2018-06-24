#pragma once

#include <thread>
#include <vector>

#include "boost/asio.hpp"

#include "thread.hpp"

namespace demo
{

    namespace asio = boost::asio;

    class io_service_executor
    {
    private: // --- scope ---
        using self = io_service_executor;
        struct alignas(64) aligned_io_service
        {
            asio::io_service _io_service;
        };
    private: // --- state ---
        std::vector<int> _cpus;
        std::vector<aligned_io_service> _io_services;
        std::size_t _next = 0;
    public: // --- life ---
        explicit io_service_executor(std::vector<int> cpus)
            : _cpus(std::move(cpus)), _io_services(_cpus.size())
        { }
        io_service_executor(const self& rhs) = delete;
        io_service_executor(const self&& rhs) noexcept = delete;
        ~io_service_executor() noexcept = default;
    public: // --- operations ---
        auto operator=(const self& rhs) & -> self = delete;
        auto operator=(self&& rhs) & noexcept -> self = delete;
        auto get_io_service() -> asio::io_service&
        {
            auto index = std::exchange(_next, (_next + 1) % _io_services.size());
            return _io_services[index]._io_service;
        }
        void run()
        {
            /* Bei den Tests sollte ueberprueft werden, dass die Cores
             * tatsaechlich gleichmaessig benutzt werden (mittels perf). Denn
             * aufgrund der Lokalitaet zu den NIC-IRQs koennte es theoretisch
             * Unterschiede geben, die das Ergebnis verzerrren. */
            std::vector<std::thread> threads;
            for (std::size_t i = 0; i < _cpus.size(); ++i) {
                threads.emplace_back([this, i] {
                        thread_affinity({_cpus[i]});
                        asio::io_service::work guard(_io_services[i]._io_service);
                        _io_services[i]._io_service.run();
                    });
            }
            for (auto&& thread : threads) {
                thread.join();
            }
        }
    };

}
