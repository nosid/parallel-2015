#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "buffer.hpp"
#include "command_line.hpp"
#include "partition.hpp"
#include "tcp.hpp"
#include "thread.hpp"

namespace
{

    using namespace std::chrono_literals;
    using namespace demo;

    class stream
    {
    private: // --- state ---
        tcp::socket _socket;
        buffer _buffer;
    public: // --- life ---
        explicit stream(tcp::socket socket)
            : _socket(std::move(socket))
        { }
    public: // --- operations ---
        auto data() { return _buffer.data(); }
        auto available() const { return _buffer.available(); }
        void drain(std::size_t n) { _buffer.drain(n); }
        auto getline(const deadline& deadline) -> std::size_t
        {
            std::size_t count = _buffer.available();
            do {
                auto p = _buffer.data(), q = p + _buffer.available();
                auto r = std::find(q - count, q, '\n');
                if (r != q) {
                    return static_cast<std::size_t>(r - p) + 1;
                }
            } while ((count = _read_some(1500, deadline)));
            return 0;
        }
        void write_n(const char* data, std::size_t size, const deadline& deadline)
        {
            for (std::size_t n = 0; n != size; ) {
                n += _socket.send_some(data + n, size - n, deadline);
            }
        }
    private:
        auto _read_some(std::size_t min_size, const deadline& deadline) -> std::size_t
        {
            _buffer.reserve(min_size);
            auto n = _socket.recv_some(_buffer.next(), _buffer.reserve(), deadline);
            _buffer.advance(n);
            return n;
        }
    };


    class session
    {
    private: // --- state ---
        stream _stream;
    public: // --- life ---
        explicit session(tcp::socket socket)
            : _stream(std::move(socket))
        {
            _run();
        }
    private: // --- operations ---
        void _run()
        {
            try {
                auto timeout = 300s;
                deadline deadline(timeout);
                while (auto length = _stream.getline(deadline)) {
                    auto data = _stream.data();
                    std::reverse(data, data + length - 1);
                    _stream.write_n(data, length, deadline);
                    _stream.drain(length);
                    deadline.expires_from_now(timeout);
                }
                if (_stream.available()) {
                    throw std::runtime_error("protocol-error");
                }
            } catch (...) {
                _handle_error();
            }
        }
        void _handle_error()
        {
            try {
                throw;
            } catch (const std::exception& e) {
                std::cerr << "EXCEPTION: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "EXCEPTION: unknown" << std::endl;
            }
        }
    };


    class queue
    {
    private: // --- state ---
        std::mutex _mutex;
        std::condition_variable _empty;
        std::vector<tcp::socket> _sockets;
    public: // --- operations ---
        void push(tcp::socket socket)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            bool notify = _sockets.empty();
            _sockets.push_back(std::move(socket));
            lock.unlock();
            if (notify) {
                _empty.notify_one();
            }
        }
        auto pop()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _empty.wait(lock, [this] { return !_sockets.empty(); });
            std::vector<tcp::socket> result;
            swap(_sockets, result);
            return result;
        }
    };


    [[noreturn]]
    void worker(queue& queue, unsigned short port, std::vector<int> cpus)
    {
        thread_affinity(cpus);
        tcp::acceptor acceptor(port, 1 << 14);
        deadline deadline(3600s);
        for (;;) {
            queue.push(tcp::socket(acceptor, deadline));
        }
    }

}


int main(int argc, char* argv[])
{
    try {
        std::ios::sync_with_stdio(false);
        // command line arguments
        std::vector<unsigned short> ports{9999};
        std::vector<int> cpus(std::thread::hardware_concurrency());
        std::iota(cpus.begin(), cpus.end(), 0);
        parse_command_line(std::cout, argc - 1, argv + 1,
            "local-ports", ports,
            "cpu-set", cpus);
        // run
        queue queue;
        std::vector<std::thread> threads;
        for (auto&& port : ports) {
            threads.emplace_back(worker, std::ref(queue), port, cpus);
        }
        std::mt19937 random(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(0, cpus.size() - 1);
        for (;;) {
            for (auto&& socket : queue.pop()) {
                auto cpu = cpus[dist(random)];
                std::thread([cpu,socket=std::move(socket)]() mutable {
                        thread_affinity({cpu});
                        session(std::move(socket));
                    }).detach();
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
