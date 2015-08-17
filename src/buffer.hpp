#pragma once

#include <algorithm>
#include <cstdlib>

namespace demo
{

    class buffer final
    {
    private: // --- scope ---
        using self = buffer;
    private: // --- state ---
        char* _data = nullptr;
        std::size_t _capacity = 0;
        std::size_t _bias = 0;
        std::size_t _size = 0;
    public: // --- life ---
        explicit buffer() noexcept = default;
        buffer(const self& rhs) = delete;
        buffer(self&& rhs) noexcept = delete;
        ~buffer() noexcept
        {
            std::free(_data);
        }
    public: // --- operations ---
        auto operator=(const self& rhs) & -> self& = delete;
        auto operator=(self&& rhs) & noexcept -> self& = delete;
        void reserve(std::size_t required)
        {
            if (_size == 0) {
                _bias = 0;
            }
            if (_bias + _size + required <= _capacity) {
                // nothing to do
            } else if (_size + required > _capacity || _size > _bias) {
                _allocate(required);
            } else {
                std::copy_n(_data + _bias, _size, _data);
                _bias = 0;
            }
        }
        void drain(std::size_t count)
        {
            _bias += count;
            _size -= count;
        }
        void advance(std::size_t count)
        {
            _size += count;
        }
        auto data() { return _data + _bias; }
        auto available() const { return _size; }
        auto next() { return _data + _bias + _size; }
        auto reserve() const { return _capacity - _bias - _size; }
    private:
        void _allocate(std::size_t required)
        {
            auto capacity = _capacity + _capacity / 2 + 24;
            if (_size > _capacity / 2) {
                capacity = std::max(capacity, _bias + _size + required);
                if (auto data = static_cast<char*>(std::realloc(_data, capacity))) {
                    _data = data;
                    _capacity = capacity;
                } else {
                    throw std::bad_alloc{};
                }
            } else {
                capacity = std::max(capacity, _size + required);
                if (auto data = static_cast<char*>(std::malloc(capacity))) {
                    std::copy_n(_data + _bias, _size, data);
                    std::free(_data);
                    _data = data;
                    _capacity = capacity;
                } else {
                    throw std::bad_alloc{};
                }
            }
        }
    };

}
