#ifndef __XQ_UTILS_RING_BUF_HPP__
#define __XQ_UTILS_RING_BUF_HPP__


#include <atomic>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <sys/uio.h>
#include "xq/utils/memory.hpp"
#include "xq/utils/log.hpp"


namespace xq::utils {


class RingBuf {
    // 禁止拷贝和移动
    RingBuf(const RingBuf&) = delete;
    RingBuf& operator=(const RingBuf&) = delete;
    RingBuf(RingBuf&&) = delete;
    RingBuf& operator=(RingBuf&&) = delete;


public:
    explicit RingBuf(size_t capacity) noexcept {
        if (capacity < 1) {
            capacity = 4096;
        }

        size_t cap = 1;
        while (cap < capacity) {
            ASSERT(cap <= (SIZE_MAX >> 1), "RingBuf capacity overflow: {}", capacity);
            cap <<= 1;
        }

        cap_ = cap;
        mask_ = cap - 1;
        buf_ = (char*)xq::utils::malloc(cap_);
        ASSERT(buf_ != nullptr, "RingBuf alloc failed, capacity: {}", cap_);
    }


    ~RingBuf() noexcept {
        xq::utils::free(buf_);
    }

    
    size_t
    capacity() const noexcept {
        return cap_;
    }


    void
    reset(size_t capacity) noexcept {
        if (buf_) {
            xq::utils::free(buf_);
            buf_ = nullptr;
        }

        buf_ = nullptr;
        cap_ = 0;
        mask_ = 0;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);

        if (capacity < 1) {
            capacity = 4096;
        }

        size_t cap = 1;
        while (cap < capacity) {
            ASSERT(cap <= (SIZE_MAX >> 1), "RingBuf capacity overflow: {}", capacity);
            cap <<= 1;
        }

        cap_ = cap;
        mask_ = cap - 1;
        buf_ = (char*)xq::utils::malloc(cap_);
        ASSERT(buf_ != nullptr, "RingBuf alloc failed, capacity: {}", cap_);
    }


    size_t
    readable() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_relaxed);
    }


    size_t
    writable() const noexcept {
        return cap_ - (tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire));
    }


    bool
    empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }


    size_t
    write(const char* src, size_t len) noexcept {
        size_t avail = writable();
        size_t n = std::min(len, avail);
        if (n == 0) {
            return 0;
        }

        size_t tail = tail_.load(std::memory_order_relaxed) & mask_;
        size_t first = std::min(n, cap_ - tail);
        ::memcpy(buf_ + tail, src, first);
        if (n > first) {
            ::memcpy(buf_, src + first, n - first);
        }

        tail_.fetch_add(n, std::memory_order_release);
        return n;
    }


    int
    write_iov(iovec iov[2], size_t max_len = SIZE_MAX) noexcept {
        size_t avail = writable();
        size_t n = std::min(max_len, avail);
        if (n == 0) return 0;

        size_t tail = tail_.load(std::memory_order_relaxed) & mask_;
        size_t first = std::min(n, cap_ - tail);

        iov[0].iov_base = buf_ + tail;
        iov[0].iov_len  = first;

        if (n > first) {
            iov[1].iov_base = buf_;
            iov[1].iov_len  = n - first;
            return 2;
        }

        return 1;
    }


    void
    write_commit(size_t n) noexcept {
        tail_.fetch_add(n, std::memory_order_release);
    }


    size_t
    read(char* dst, size_t len) noexcept {
        size_t n = peek(dst, len);
        if (n > 0) {
            head_.fetch_add(n, std::memory_order_release);
        }

        return n;
    }


    size_t
    peek(char* dst, size_t len) const noexcept {
        size_t avail = readable();
        size_t n = std::min(len, avail);
        if (n == 0) {
            return 0;
        }

        size_t head  = head_.load(std::memory_order_relaxed) & mask_;
        size_t first = std::min(n, cap_ - head);
        ::memcpy(dst, buf_ + head, first);
        if (n > first) ::memcpy(dst + first, buf_, n - first);

        return n;
    }


    size_t
    skip(size_t n) noexcept {
        size_t avail = readable();
        n = std::min(n, avail);
        head_.fetch_add(n, std::memory_order_release);
        return n;
    }


    int
    read_iov(iovec iov[2], size_t max_len = SIZE_MAX) const noexcept {
        size_t avail = readable();
        size_t n = std::min(max_len, avail);
        if (n == 0) {
            return 0;
        }

        size_t head  = head_.load(std::memory_order_relaxed) & mask_;
        size_t first = std::min(n, cap_ - head);

        iov[0].iov_base = buf_ + head;
        iov[0].iov_len  = first;

        if (n > first) {
            iov[1].iov_base = buf_;
            iov[1].iov_len  = n - first;
            return 2;
        }

        return 1;
    }


    void
    read_consume(size_t n) noexcept {
        head_.fetch_add(n, std::memory_order_release);
    }


    void
    clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }


private:
    char*  buf_;
    size_t cap_;
    size_t mask_;


    // 强隔离 head 和 tail，防止不同核心的缓存行竞争
    alignas(64) std::atomic<size_t> head_ { 0 }; 
    alignas(64) std::atomic<size_t> tail_ { 0 }; 
}; // class RingBuf


} // namespace xq::utils


#endif // __XQ_UTILS_RING_BUF_HPP__