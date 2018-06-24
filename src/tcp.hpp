#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

namespace demo
{

    class deadline
    {
    private: // --- scope ---
        using self = deadline;
        using steady_clock = std::chrono::steady_clock;
        using time_point = steady_clock::time_point;
        using duration = std::chrono::nanoseconds;
    private: // --- state ---
        int _fd = -1;
    public: // --- life ---
        explicit deadline(const duration& duration)
            : deadline()
        {
            _init_aux();
            _expires_from_now_aux(duration);
        }
        deadline(const self& rhs) = delete;
        deadline(self&& rhs) noexcept
            : deadline()
        {
            swap(rhs);
        }
        ~deadline() noexcept
        {
            if (_fd != -1) {
                ::close(_fd);
            }
        }
    private:
        explicit deadline() = default;
    public: // --- operations ---
        auto operator=(const self& rhs) & -> self& = delete;
        auto operator=(self&& rhs) & noexcept -> self&
        {
            swap(rhs);
            return *this;
        }
        void swap(self& rhs) noexcept
        {
            std::swap(_fd, rhs._fd);
        }
        friend void swap(self& lhs, self& rhs) noexcept
        {
            lhs.swap(rhs);
        }
        void wait(int fd, short events) const
        {
            pollfd fds[] = {{fd, events, 0}, {_fd, POLLIN, 0}};
            for (;;) {
                int rv = ::ppoll(fds, 2, nullptr, nullptr);
                if (rv > 0) {
                    constexpr auto valid = POLLIN | POLLOUT | POLLHUP | POLLERR;
                    if (fds[0].revents & ~valid) {
                        throw std::runtime_error("tcp-poll-error");
                    } else if (fds[1].revents & ~valid) {
                        throw std::runtime_error("tcp-poll-error");
                    } else if (fds[0].revents & valid) {
                        return;
                    } else if (fds[1].revents & valid) {
                        throw std::runtime_error("tcp-timeout");
                    } else {
                        throw std::runtime_error("tcp-poll-error");
                    }
                } else if (rv == 0) {
                    throw std::runtime_error("tcp-poll-error");
                } else if (errno == EINTR) {
                    // restart
                } else {
                    throw std::runtime_error("tcp-poll-error");
                }
            }
        }
        void expires_from_now(const duration& duration)
        {
            if (!_expires_from_now_aux(duration)) {
                _init_aux();
                _expires_from_now_aux(duration);
            }
        }
    private:
        bool _expires_from_now_aux(const duration& duration)
        {
            auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - s);
            itimerspec new_value = {{0, 0}, {s.count(), ns.count()}};
            itimerspec old_value = {{0, 0}, {0, 0}};
            if (::timerfd_settime(_fd, 0, &new_value, &old_value) != 0) {
                throw std::runtime_error("timerfd-settime-error");
            }
            return old_value.it_value.tv_sec || old_value.it_value.tv_nsec;
        }
        void _init_aux()
        {
            int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
            if (fd == -1) {
                throw std::runtime_error("timerfd-error");
            }
            std::swap(fd, _fd);
            if (fd != -1) {
                ::close(fd);
            }
        }
    };


    template <typename Type>
    void setsockopt_aux(int fd, int level, int option, Type&& value)
    {
        if (::setsockopt(fd, level, option, &value, sizeof(value)) != 0) {
            throw std::runtime_error("tcp-socket-option-error");
        }
    }


    class tcp
    {
    public: // --- scope ---
        class acceptor;
        class socket;
    };


    class tcp::acceptor
    {
    private: // --- scope ---
        using self = acceptor;
    private: // --- state ---
        int _fd = -1;
    public: // --- life ---
        explicit acceptor(unsigned short port, int backlog)
            : acceptor()
        {
            _fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (_fd == -1) {
                throw std::runtime_error("tcp-socket-error");
            }
            setsockopt_aux(_fd, SOL_SOCKET, SO_REUSEADDR, int(1));
            setsockopt_aux(_fd, SOL_SOCKET, SO_REUSEPORT, int(1));
            sockaddr_in sa;
            sa.sin_family = AF_INET;
            sa.sin_port = htons(port);
            sa.sin_addr.s_addr = htonl(INADDR_ANY);
            if (::bind(_fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
                throw std::runtime_error("tcp-bind-error");
            }
            if (::listen(_fd, backlog) != 0) {
                throw std::runtime_error("tcp-listen-error");
            }
        }
        acceptor(const self& rhs) = delete;
        acceptor(self&& rhs) noexcept
            : acceptor()
        {
            swap(rhs);
        }
        ~acceptor() noexcept
        {
            if (_fd != -1) {
                ::close(_fd);
            }
        }
    private:
        explicit acceptor() = default;
    public: // --- operations ---
        auto operator=(const self& rhs) & -> self& = delete;
        auto operator=(self&& rhs) & noexcept -> self&
        {
            swap(rhs);
            return *this;
        }
        void swap(self& rhs) noexcept
        {
            std::swap(_fd, rhs._fd);
        }
        friend void swap(self& lhs, self& rhs) noexcept
        {
            lhs.swap(rhs);
        }
        auto get_native_handle() -> int
        {
            return _fd;
        }
    };


    class tcp::socket
    {
    private: // --- scope ---
        using self = socket;
    private: // --- state ---
        sockaddr_in _peer;
        int _fd = -1;
        bool _wait_recv = false;
        bool _wait_send = false;
    public: // --- life ---
        explicit socket(acceptor& acceptor, deadline& deadline)
            : socket()
        {
            bool waited = false;
            socklen_t length = sizeof(_peer);
            for (;;) {
                _fd = ::accept4(acceptor.get_native_handle(),
                    reinterpret_cast<sockaddr*>(&_peer), &length, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (_fd != -1) {
                    setsockopt_aux(_fd, IPPROTO_TCP, TCP_NODELAY, int(1));
                    break;
                } else if (!waited && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    deadline.wait(acceptor.get_native_handle(), POLLIN);
                    waited = true;
                } else if (errno == EINTR) {
                    // restart
                } else {
                    throw std::runtime_error("tcp-accept-error");
                }
            }
        }
        socket(const self& rhs) = delete;
        socket(self&& rhs) noexcept
            : socket()
        {
            swap(rhs);
        }
        ~socket() noexcept
        {
            if (_fd != -1) {
                ::close(_fd);
            }
        }
    private:
        explicit socket() = default;
    public: // --- operations ---
        auto operator=(const self& rhs) & -> self& = delete;
        auto operator=(self&& rhs) & noexcept -> self&
        {
            swap(rhs);
            return *this;
        }
        void swap(self& rhs) noexcept
        {
            std::swap(_peer, rhs._peer);
            std::swap(_fd, rhs._fd);
            std::swap(_wait_recv, rhs._wait_recv);
            std::swap(_wait_send, rhs._wait_send);
        }
        friend void swap(self& lhs, self& rhs) noexcept
        {
            lhs.swap(rhs);
        }
        auto recv_some(char* data, std::size_t size, const deadline& deadline) -> std::size_t
        {
            if (_wait_recv) {
                deadline.wait(_fd, POLLIN);
            }
            for (;;) {
                ssize_t rv = ::recv(_fd, data, size, MSG_NOSIGNAL);
                if (rv != -1) {
                    _wait_recv = std::size_t(rv) < size;
                    return static_cast<std::size_t>(rv);
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    deadline.wait(_fd, POLLIN);
                } else {
                    throw std::runtime_error("tcp-send-error");
                }
            }
        }
        auto send_some(const char* data, std::size_t size, const deadline& deadline) -> std::size_t
        {
            if (_wait_send) {
                deadline.wait(_fd, POLLOUT);
            }
            for (;;) {
                ssize_t rv = ::send(_fd, data, size, MSG_NOSIGNAL);
                if (rv != -1) {
                    _wait_send = std::size_t(rv) < size;
                    return static_cast<std::size_t>(rv);

                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    deadline.wait(_fd, POLLOUT);
                } else {
                    throw std::runtime_error("tcp-send-error");
                }
            }
        }
    };

}
