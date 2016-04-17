#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "boost/asio.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/asio/system_timer.hpp"

#include "command_line.hpp"
#include "log.hpp"
#include "partition.hpp"
#include "thread.hpp"


namespace
{

    using namespace std::chrono_literals;
    namespace asio = boost::asio;
    using namespace demo;

    using seconds = std::chrono::seconds;
    using milliseconds = std::chrono::milliseconds;
    using microseconds = std::chrono::microseconds;
    using nanoseconds = std::chrono::nanoseconds;

    using error_code = boost::system::error_code;
    using tcp = asio::ip::tcp;

    using clock = std::chrono::system_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double>;

    using owner = std::shared_ptr<void>;


    void append(std::ostream& os __attribute__((unused))) { }

    template <typename Head, typename... Tail>
    void append(std::ostream& os, Head&& head, Tail&&... tail)
    {
        os << std::forward<Head>(head);
        append(os, std::forward<Tail>(tail)...);
    }

    template <typename... Args>
    void abort_on_error_aux(error_code ec, Args&&... args)
    {
        if (ec) {
            std::ostringstream os;
            os << "ERROR[" << ec << "]:";
            append(os, std::forward<Args>(args)..., '\n');
            std::cerr << os.str();
            std::terminate();
        }
    }

#define ABORT_ON_ERROR(ec, ...) \
    do { abort_on_error_aux(ec, " file:", __FILE__, " line:", __LINE__, " func:", __PRETTY_FUNCTION__, __VA_ARGS__); } while (false)


    class chunk final
    {
    private: // --- state ---
        char* _data;
        std::size_t _size;
    public: // --- life ---
        explicit chunk(char* data, std::size_t size)
            : _data(data), _size(size)
        { }
    public: // --- operations ---
        auto data() { return _data; }
        auto size() { return _size; }
    };


    class chunker final
    {
    private: // --- state ---
        std::size_t _size;
        std::mt19937 _random;
        std::uniform_int_distribution<std::size_t> _dist;
        std::unique_ptr<char[]> _data;
    public: // --- life ---
        explicit chunker(std::size_t size)
            : _size(size)
            , _random(std::random_device()())
            , _dist(0, _size - 1)
            , _data(std::make_unique<char[]>(_size))
        {
            auto dist = std::uniform_int_distribution<char>('A', 'Z');
            for (std::size_t i = 0; i + 1 < _size; ++i) {
                _data[i] = dist(_random);
            }
            _data[_size - 1] = '\n';
        }
    public: // --- operations ---
        auto operator()() -> chunk
        {
            std::size_t offset = _dist(_random);
            return chunk(_data.get() + offset, _size - offset);
        }
    };


    class session final
    {
    private: // --- scope ---
        class request final
        {
        private: // --- state ---
            chunk _chunk;
            std::function<void()> _handler;
        public: // --- life ---
            template <typename Handler>
            explicit request(chunk chunk, Handler&& handler)
                : _chunk(chunk), _handler(std::forward<Handler>(handler))
            { }
        public: // --- operations ---
            auto get_chunk() const { return _chunk; }
            void done() const { _handler(); }
        };
        using requests = std::list<request>;
    private: // --- state ---
        tcp::socket _socket;
        tcp::endpoint _peer;
        bool _send_lock = false;
        bool _recv_lock = false;
        requests _send_reqs;
        requests _recv_reqs;
    public: // --- life ---
        explicit session(asio::io_service& io_service, tcp::endpoint peer)
            : _socket(io_service), _peer(std::move(peer))
        { }
    public: // --- operations ---
        template <typename Handler>
        void async_connect(Handler&& handler)
        {
            _socket.async_connect(_peer, [this,handler=std::forward<Handler>(handler)](error_code ec) mutable {
                    if (!ec) {
                        _socket.set_option(tcp::no_delay(true));
                    }
                    handler(ec);
                });
        }
        template <typename Handler>
        void async_roundtrip(chunk chunk, Handler&& handler)
        {
            _dispatch(_send_lock, _send_reqs, &session::_async_send,
                request(chunk, std::forward<Handler>(handler)));
        }
    private:
        void _async_send(request req)
        {
            auto chunk = req.get_chunk();
            async_write(_socket, asio::buffer(chunk.data(), chunk.size()),
                [this,req=std::move(req)](error_code ec, std::size_t) {
                    ABORT_ON_ERROR(ec, " action:async-send");
                    _dequeue(_send_lock, _send_reqs, &session::_async_send);
                    _dispatch(_recv_lock, _recv_reqs, &session::_async_recv, std::move(req));
                });
        }
        void _async_recv(request req)
        {
            auto chunk = req.get_chunk();
            async_read(_socket, asio::buffer(chunk.data(), chunk.size()),
                [this,req=std::move(req)](error_code ec, std::size_t) {
                    ABORT_ON_ERROR(ec, " action:async-recv");
                    _dequeue(_recv_lock, _recv_reqs, &session::_async_recv);
                    req.done();
                });
        }
        void _dispatch(bool& lock, requests& reqs, void (session::*handler)(request), request req)
        {
            if (lock) {
                reqs.emplace_back(std::move(req));
            } else {
                lock = true;
                (this->*handler)(std::move(req));
            }
        }
        void _dequeue(bool& lock, requests& reqs, void (session::*handler)(request))
        {
            if (reqs.empty()) {
                lock = false;
            } else {
                auto req = reqs.front();
                reqs.pop_front();
                (this->*handler)(std::move(req));
            }
        }
    };


    class dispatcher final
    {
    private: // --- scope ---
        template <typename Handler>
        class connector final
        {
        public: // --- state ---
            Handler _handler;
            std::size_t _index;
            std::size_t _pending = 0;
        public: // --- life ---
            explicit connector(Handler handler, std::size_t index)
                : _handler(std::move(handler)), _index(index)
            { }
        };
    private: // --- state ---
        std::mt19937 _random;
        std::vector<session> _sessions;
        std::size_t _bulk_connect;
    public: // --- life ---
        explicit dispatcher(
            asio::io_service& io_service,
            const std::vector<tcp::endpoint>& endpoints,
            std::size_t bulk_connect)
            : _random(std::random_device()()), _bulk_connect(bulk_connect)
        {
            _sessions.reserve(endpoints.size());
            for (auto&& endpoint : endpoints) {
                _sessions.emplace_back(io_service, endpoint);
            }
        }
    public: // --- operations ---
        template <typename Handler>
        void async_connect(Handler handler)
        {
            _async_connect_bulk(std::make_shared<connector<Handler>>(std::move(handler), _sessions.size()));
        }
        template <typename Handler>
        void async_roundtrip(chunk chunk, Handler&& handler)
        {
            auto index = std::uniform_int_distribution<std::size_t>(0, _sessions.size() - 1)(_random);
            _sessions[index].async_roundtrip(chunk, std::forward<Handler>(handler));
        }
    private:
        template <typename Handler>
        void _async_connect_bulk(std::shared_ptr<connector<Handler>> connector)
        {
            while (connector->_index > 0 && connector->_pending <= _bulk_connect) {
                --connector->_index;
                ++connector->_pending;
                _sessions[connector->_index].async_connect(
                    [this,connector](error_code ec) {
                        ABORT_ON_ERROR(ec, " action:async-connect");
                        --connector->_pending;
                        _async_connect_bulk(connector);
                    });
            }
            if (connector->_index == 0 && connector->_pending == 0) {
                connector->_handler();
            }
        }
    };


    class controller final
    {
    private: // --- scope ---
        class record final
        {
        public: // --- state ---
            std::size_t _count;
            double _requests;
            duration _latencies;
        public: // --- life ---
            explicit record()
                : _count(), _requests(), _latencies()
            { }
            explicit record(std::size_t count, double requests, duration latencies)
                : _count(count), _requests(requests), _latencies(latencies)
            { }
        public: // --- operations ---
            auto split(double ratio) -> record
            {
                record result(_count, _requests * ratio, _latencies * ratio);
                _requests -= result._requests;
                _latencies -= result._latencies;
                return result;
            }
            void add(const record& other)
            {
                _count += other._count;
                _requests += other._requests;
                _latencies += other._latencies;
            }
        };
        using Records = std::map<time_point, record>;
    private: // --- state ---
        std::mutex _mutex;
        std::size_t _count;
        time_point _watermark;
        milliseconds _interval;
        Records _records;
        record _record;
        record _current;
    public: // --- life ---
        explicit controller(std::size_t count, time_point watermark)
            : _count(count), _watermark(watermark), _interval(5000ms)
        {
            _records.emplace(watermark, record());
            auto time_since_epoch = std::chrono::duration_cast<milliseconds>(_watermark.time_since_epoch());
            time_since_epoch = time_since_epoch + _interval - (time_since_epoch % _interval);
            _watermark = time_point(time_since_epoch);
        }
    public: // --- operations ---
        void update(time_point from, time_point to, double completed, duration latencies, int pending, duration awaiting)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            auto p = _put(from);
            auto q = _put(to);
            auto elapsed = duration(to - from);
            for (; p != q; ++p) {
                auto r = std::next(p);
                auto ratio = duration(r->first - p->first) / elapsed;
                r->second.add(record(1, completed * ratio, latencies * ratio));
            }
            _current._requests += pending;
            _current._latencies += awaiting;
            _drain();
        }
    private:
        auto _put(time_point tp) -> Records::iterator
        {
            auto p = _records.lower_bound(tp);
            if (p == _records.end()) {
                return _records.emplace_hint(p, tp, record());
            } else if (p->first == tp) {
                return p;
            } else {
                auto q = std::prev(p);
                auto ratio = duration(tp - q->first) / duration(p->first - q->first);
                return _records.emplace_hint(p, tp, p->second.split(ratio));
            }
        }
        void _drain()
        {
            auto p = _records.begin(), q = std::next(p), r = _records.end();
            while (q != r && q->second._count == _count) {
                _finish(p->first, q->first, q->second);
                _records.erase(p);
                p = q++;
            }
        }
        void _finish(time_point from, time_point to, record current)
        {
            while (to >= _watermark) {
                auto ratio = duration(_watermark - from) / duration(to - from);
                _record.add(current.split(ratio));
                std::cout << "STATUS: "
                          << std::chrono::duration_cast<seconds>(to.time_since_epoch()).count()
                          << " "
                          << static_cast<std::size_t>(duration(1s) / _interval * _record._requests)
                          << " "
                          << std::chrono::duration_cast<microseconds>(_record._latencies / (_record._requests + 1)).count()
                          << " "
                          << static_cast<std::size_t>(_current._requests)
                          << " "
                          << std::chrono::duration_cast<microseconds>(_current._latencies / (_current._requests + 1)).count()
                          << std::endl;
                from = _watermark;
                _watermark += _interval;
                _record = record();
            }
            _record.add(current);
        }
    };


    class scheduler final
    {
    private: // --- scope ---
        class state final
        {
        public: // --- state ---
            int _count = 0;
            duration _duration;
        public: // --- life ---
            explicit state() = default;
            explicit state(int count, duration duration)
                : _count(count), _duration(duration)
            { }
        };
    private: // --- state ---
        std::shared_ptr<controller> _controller;
        time_point _watermark;
        double _rps;
        int _threshold;
        time_point _base = clock::now();
        state _pending;
        state _previous;
        state _completed;
    public: // --- life ---
        explicit scheduler(
            std::shared_ptr<controller> controller,
            time_point watermark,
            double rps,
            int threshold)
            : _controller(std::move(controller))
            , _watermark(watermark)
            , _rps(rps)
            , _threshold(threshold)
        { }
    public: // --- operations ---
        auto initiated(time_point now) -> duration
        {
            auto interval = 1.0s / _rps;
            _pending._count += 1;
            _pending._duration += now - _base;
            if (_pending._count > _threshold) {
                interval += interval * (1.0 * _pending._count / _threshold);
            }
            return interval;
        }
        void completed(time_point now, duration elapsed)
        {
            _pending._count -= 1;
            _pending._duration -= now - elapsed - _base;
            _completed._count += 1;
            _completed._duration += elapsed;
            if (now - _watermark >= 100ms) {
                auto latencies = _pending._count * (now - _base) - _pending._duration;
                _controller->update(
                    _watermark, now,
                    _completed._count, _completed._duration,
                    _pending._count - _previous._count, latencies - _previous._duration);
                _completed = state();
                _previous = state(_pending._count, latencies);
                _watermark = now;
            }
        }
    };


    class driver final : public std::enable_shared_from_this<driver>
    {
    private: // --- state ---
        asio::system_timer _timer;
        dispatcher _dispatcher;
        scheduler _scheduler;
        chunker _chunker;
        time_point _watermark;
    public: // --- life ---
        explicit driver(
            asio::io_service& io_service,
            const std::vector<tcp::endpoint>& endpoints,
            std::size_t bulk_connect,
            scheduler scheduler,
            chunker chunker)
            : _timer(io_service)
            , _dispatcher(io_service, endpoints, bulk_connect)
            , _scheduler(std::move(scheduler))
            , _chunker(std::move(chunker))
        { }
    public: // --- operations ---
        void async_run()
        {
            auto self = shared_from_this();
            _dispatcher.async_connect([this,self=std::move(self)] {
                    _watermark = clock::now();
                    _async_run(std::move(self));
                });
        }
    private:
        void _async_run(std::shared_ptr<driver> self)
        {
            auto horizon = clock::now();
            while (_watermark <= horizon) {
                _dispatcher.async_roundtrip(_chunker(), [this,self,start=horizon] {
                        auto now = clock::now();
                        _scheduler.completed(now, now - start);
                    });
                _watermark += std::chrono::duration_cast<clock::duration>(
                    _scheduler.initiated(horizon));
            }
            _timer.expires_at(_watermark);
            _timer.async_wait([this,self=std::move(self)](error_code ec) {
                    ABORT_ON_ERROR(ec, "async-wait");
                    _async_run(std::move(self));
                });
        }
    };

}

int main(int argc, char* argv[])
{
    try {
        std::ios::sync_with_stdio(false);
        // command line arguments
        std::string addr = "127.0.0.1";
        std::vector<unsigned short> ports{9999};
        std::size_t connections = 100;
        std::size_t rps = 1000;
        std::size_t range = 100;
        std::vector<int> cpus(std::thread::hardware_concurrency());
        std::iota(cpus.begin(), cpus.end(), 0);
        std::size_t bulk_connect = SOMAXCONN;
        parse_command_line(std::cout, argc - 1, argv + 1,
            "remote-addr", addr,
            "remote-ports", ports,
            "connections", connections,
            "requests-per-second", rps,
            "message-size-range", range,
            "cpu-set", cpus,
            "bulk-connect", bulk_connect);
        // run
        auto address = asio::ip::address::from_string(addr);
        std::vector<tcp::endpoint> endpoints;
        for (std::size_t i = 0; i != connections; ++i) {
            endpoints.emplace_back(address, ports[i % ports.size()]);
        }
        auto&& rps_ = partition(rps, cpus.size());
        auto&& connections_ = partition(connections, cpus.size());
        auto&& bulk_connect_ = partition(bulk_connect, cpus.size());
        time_point watermark = clock::now();
        auto controller = std::make_shared<class controller>(cpus.size(), watermark);
        std::vector<std::thread> threads;
        for (auto cpu : cpus) {
            auto q = endpoints.end(), p = q - connections_();
            threads.emplace_back(
                [cpu,endpoints=std::vector<tcp::endpoint>(p, q),range,watermark,controller,rps=rps_(),bulk_connect=bulk_connect_()] {
                    thread_affinity({cpu});
                    auto threshold = static_cast<int>(endpoints.size());
                    asio::io_service io_service;
                    std::make_shared<driver>(io_service, endpoints, bulk_connect, scheduler(controller, watermark, rps, threshold), chunker(range))->async_run();
                    io_service.run();
                });
            endpoints.erase(p, q);
        }
        for (auto&& thread : threads) {
            thread.join();
        }
    } catch (std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
