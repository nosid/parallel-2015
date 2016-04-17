#include <iostream>

#include "boost/asio/steady_timer.hpp"

#include "buffer.hpp"
#include "command_line.hpp"
#include "io_service_executor.hpp"
#include "log.hpp"

namespace
{

    using namespace std::chrono_literals;
    using namespace demo;
    using error_code = boost::system::error_code;


    class stream final
    {
    private: // --- state ---
        asio::ip::tcp::socket _socket;
        asio::ip::tcp::endpoint _peer;
        buffer _buffer;
        asio::steady_timer _timer;
        bool _timeout = false;
    public: // --- life ---
        explicit stream(asio::ip::tcp::socket socket, asio::ip::tcp::endpoint peer)
            : _socket(std::move(socket)), _peer(std::move(peer)), _timer(_socket.get_io_service())
        {
            _socket.set_option(asio::ip::tcp::no_delay(true));
        }
    public: // --- operations ---
        auto data() { return _buffer.data(); }
        auto available() const { return _buffer.available(); }
        void drain(std::size_t n) { _buffer.drain(n); }
        bool timeout() const { return _timeout; }
        void expires_from_now(const asio::steady_timer::duration& duration, std::shared_ptr<void> owner)
        {
            _timer.expires_from_now(duration);
            _timer.async_wait(
                [this,owner=std::move(owner)](error_code ec) mutable {
                    if (ec == asio::error::operation_aborted) {
                        // ignore
                    } else if (ec) {
                        log("WARN: timer error: ", ec);
                    } else {
                        _timeout = true;
                        _socket.cancel();
                    }
                });
        }
        template <typename Handler>
        void async_getline(Handler handler, std::size_t offset = 0)
        {
            auto p = _buffer.data(), q = p + _buffer.available();
            auto r = std::find(p + offset, q, '\n');
            if (r != q) {
                handler(error_code(), r - p + 1);
            } else {
                try {
                    _buffer.reserve(1500);
                } catch (const std::bad_alloc&) {
                    handler(boost::asio::error::no_memory, _buffer.available());
                    return;
                }
                _socket.async_read_some(
                    asio::buffer(_buffer.next(), _buffer.reserve()),
                    [this,handler=std::move(handler)](error_code ec, std::size_t count) mutable {
                        if (ec) {
                            handler(ec, _buffer.available());
                        } else {
                            _buffer.advance(count);
                            async_getline(std::move(handler), _buffer.available() - count);
                        }
                    });
            }
        }
        template <typename Handler>
        void async_write_n(const char* data, std::size_t size, Handler handler)
        {
            async_write(_socket, asio::buffer(data, size), std::move(handler));
        }
        bool good(error_code ec)
        {
            return !_timeout && !ec;
        }
        void release()
        {
            if (_socket.is_open()) {
                _socket.close();
            }
            _timer.cancel();
        }
    };


    class session final : public std::enable_shared_from_this<session>
    {
    private: // --- state ---
        stream _stream;
    public: // --- life ---
        explicit session(asio::ip::tcp::socket socket, asio::ip::tcp::endpoint peer)
            : _stream(std::move(socket), std::move(peer))
        { }
    public: // --- operations ---
        void start()
        {
            _async_run(shared_from_this());
        }
    private:
        void _async_run(std::shared_ptr<session> self)
        {
            _stream.expires_from_now(300s, self);
            _stream.async_getline(
                [this,self=std::move(self)](error_code ec, std::size_t length) mutable {
                    if (_stream.good(ec)) {
                        auto data = _stream.data();
                        std::reverse(data, data + length - 1);
                        _stream.async_write_n(data, length,
                            [this,self=std::move(self)](error_code ec, std::size_t length) mutable {
                                if (_stream.good(ec)) {
                                    _stream.drain(length);
                                    _async_run(std::move(self));
                                } else {
                                    _handle_error(ec, "sending data to client");
                                }
                            });
                    } else {
                        _handle_error(ec, "receiving line from client");
                    }
                });
        }
        void _handle_error(error_code ec, const char* operation)
        {
            if (_stream.timeout()) {
                log("WARN: operation timeout: ", operation);
            } else if (ec != asio::error::eof) {
                log("WARN: operation error: ", operation);
            } else if (_stream.available()) {
                log("WARN: protocol violation");
            } else {
                // eof
            }
            _stream.release();
        }
    };


    class server final
    {
    private: // --- state ---
        io_service_executor& _executor;
        asio::ip::tcp::acceptor _acceptor;
        asio::ip::tcp::socket _socket;
        asio::ip::tcp::endpoint _peer;
    public: // --- life ---
        explicit server(io_service_executor& executor, unsigned short port)
            : _executor(executor)
            , _acceptor(executor.get_io_service(), asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
            , _socket(_executor.get_io_service())
        {
            _async_accept();
        }
    private:
        void _async_accept()
        {
            _acceptor.async_accept(_socket, _peer, [this](error_code ec) {
                    asio::ip::tcp::socket socket(std::move(_socket));
                    _socket = asio::ip::tcp::socket(_executor.get_io_service());
                    asio::ip::tcp::endpoint peer = std::move(_peer);
                    _async_accept();
                    if (ec) {
                        log("WARN: socket accept failed: ", ec);
                    } else {
                        try {
                            std::make_shared<session>(std::move(socket), std::move(peer))->start();
                        } catch (const std::bad_alloc& e) {
                            log("WARN: session create failed: ", e.what());
                        }
                    }
                });
        }
    };

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
        io_service_executor executor(cpus);
        std::vector<server> servers;
        servers.reserve(ports.size());
        for (auto port : ports) {
            servers.emplace_back(executor, port);
        }
        executor.run();
    } catch (std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
