#pragma once

#include "../memory.hpp"
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace smedley::sstd
{

    template <typename T>
    class vector
    {
    public:
        using size_type = size_t;
        using reference = T &;

        struct iterator
        {
            iterator(T *ptr) : _ptr(ptr) {}

            reference operator*() const { return *_ptr; }
            T *operator->() { return _ptr; }
            iterator &operator++() { _ptr++;  return *this; }
            iterator operator++(int) { iterator tmp = *this; ++(*this); return tmp; }
            iterator &operator--() { _ptr--;  return *this; }
            iterator operator--(int) { iterator tmp = *this; --(*this); return tmp; }
            friend bool operator==(const iterator &a, const iterator &b) { return a._ptr == b._ptr; }
            friend bool operator!=(const iterator &a, const iterator &b) { return a._ptr != b._ptr; }
        private:
            T *_ptr;
        };
    protected:
        T *_first;
        T *_last;
        T *_end;
        uint32_t _al_val;

        void _change_array(T *new_vec, size_type new_size, size_type new_capacity)
        {
            if (_first != nullptr) {
                HeapFree(memory::Map::game_heap, NULL, reinterpret_cast<void *>(_first));
            }

            _first = new_vec;
            _last = new_vec + new_size;
            _end = new_vec + new_capacity;
        }

        void _uninit_copy(T *first, size_type count, T *where)
        {
            if (count != 0) std::memcpy(where, first, count * sizeof(T));
        }
    public:
        vector() : _first(nullptr), _last(nullptr), _end(nullptr), _al_val(0)
        {
        }

        /// @param n New capacity of the internal array.
        void reserve(size_t n)
        {
            static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                          "mutable engine vectors support only trivial element types");
            if (n > capacity()) {
                if (n > (std::numeric_limits<size_type>::max)() / sizeof(T)) throw std::bad_alloc();
                const size_type old_size = size();
                T *new_vec = reinterpret_cast<T *>(HeapAlloc(memory::Map::game_heap, HEAP_ZERO_MEMORY, sizeof(T) * n));
                if (new_vec == nullptr) throw std::bad_alloc();

                _uninit_copy(_first, old_size, new_vec);
                _change_array(new_vec, old_size, n);
            }
        }

        void push_back(const T &val)
        {
            static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                          "mutable engine vectors support only trivial element types");
            if (_last == _end) {
                if (_first == nullptr) {
                    reserve(0x10);
                } else {
                    reserve(capacity() << 1);
                }
            }

            ::new (static_cast<void *>(_last)) T(val);
            ++_last;
        }

        bool erase_value(const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                          "mutable engine vectors support only trivial element types");
            for (auto *current = _first; current != _last; ++current) {
                if (*current != value) {
                    continue;
                }
                std::memmove(current, current + 1, (_last - current - 1) * sizeof(T));
                --_last;
                return true;
            }
            return false;
        }

        inline size_type capacity() const noexcept { return _first == nullptr ? 0 : _end - _first; }
        inline size_type size() const noexcept { return _first == nullptr ? 0 : _last - _first; }
        bool bounded_size(size_type maximum, size_type *result) const noexcept
        {
            if (result == nullptr) return false;
            const uintptr_t first = reinterpret_cast<uintptr_t>(_first);
            const uintptr_t last = reinterpret_cast<uintptr_t>(_last);
            const uintptr_t end = reinterpret_cast<uintptr_t>(_end);
            if (first == 0 || last == 0 || end == 0) {
                if (first != 0 || last != 0 || end != 0) return false;
                *result = 0;
                return true;
            }
            if (first > last || last > end || (last - first) % sizeof(T) != 0 || (end - first) % sizeof(T) != 0) return false;
            const size_type count = (last - first) / sizeof(T);
            if (count > maximum) return false;
            *result = count;
            return true;
        }

        inline iterator begin() { return iterator(_first); }
        inline iterator end() { return iterator(_last); }

        reference operator[](size_type i) { return _first[i]; }
        const reference operator[](size_type i) const { return _first[i]; }
    };

    static_assert(sizeof(vector<int>) == 0x10);

}
