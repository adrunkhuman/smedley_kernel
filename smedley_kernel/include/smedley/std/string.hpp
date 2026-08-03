#pragma once

#include "../memory.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>

namespace smedley::sstd
{

    template <typename T,
              class Traits = std::char_traits<T>,
              class Allocator = std::allocator<T>>
    class basic_string
    {
    public:
        using size_type = size_t;

        using reference = T &;
        using const_reference = const T &;
    protected:
        static constexpr size_type default_capacity = 0xf;

        union {
            T buf[default_capacity + 1];
            T *ptr;
        } _impl;
        size_type _size;
        size_type _capacity;
        Allocator _alloc;
    public:
        basic_string() : _size(0), _capacity(default_capacity)
        {
            _impl.buf[0] = static_cast<T>(0x0);
        }

        basic_string(const T *str)
        {
            size_type n = Traits::length(str);
            if (n > default_capacity) {
                if (n == (std::numeric_limits<size_type>::max)()
                    || n + 1 > (std::numeric_limits<size_type>::max)() / sizeof(T)) {
                    throw std::bad_alloc();
                }
                _impl.ptr = reinterpret_cast<T *>(
                    HeapAlloc(memory::Map::game_heap, 0, (n + 1) * sizeof(T)));
                if (_impl.ptr == nullptr) throw std::bad_alloc();
                std::memcpy(_impl.ptr, str, n * sizeof(T));
                _impl.ptr[n] = static_cast<T>(0);
                _capacity = n;
            } else {
                std::fill(std::begin(_impl.buf), std::end(_impl.buf), static_cast<T>(0));
                std::memcpy(_impl.buf, str, n * sizeof(T));
                _capacity = default_capacity;
            }

            _size = n;
        }

        inline const T *c_str() const {
            if (_capacity > default_capacity) {
                return _impl.ptr;
            }
            return _impl.buf;
        }

        inline size_type size() const noexcept { return _size; }
        inline size_type capacity() const noexcept { return _capacity; }

        inline reference operator[](size_type pos)
        {
            if (_capacity > default_capacity) {
                return _impl.ptr[pos];
            }
            return _impl.buf[pos];
        }

        inline const_reference operator[](size_type pos) const
        {
            if (_capacity > default_capacity) {
                return _impl.ptr[pos];
            }
            return _impl.buf[pos];
        }

        friend bool operator==(const basic_string<T> &lhs, const basic_string<T> &rhs)
        {
            return lhs.size() == rhs.size()
                && Traits::compare(lhs.c_str(), rhs.c_str(), lhs.size()) == 0;
        }
    };

    using string = basic_string<char>;

    static_assert(sizeof(string) == 0x1c);

}
