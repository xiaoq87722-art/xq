#ifndef __XQ_UTILS_MPSC_HPP__
#define __XQ_UTILS_MPSC_HPP__


#include <immintrin.h>


#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <thread>
#include <vector>


#include "xq/utils/memory.hpp"


namespace xq::utils {


template<typename T>
class MPSC {
    class SingleQueue {
    public:
        struct alignas(64) Cell {
            std::atomic<size_t> seq;
            T data;
        };


        explicit SingleQueue(size_t size) noexcept
            : size_(size), mask_(size - 1),
              buffer_((Cell*)xq::utils::malloc(sizeof(Cell) * size)) {
            ASSERT(size > 0 && (size & (size - 1)) == 0, "SingleQueue 的 size 必须是 2 的幂次方");
            for (size_t i = 0; i < size_; ++i) {
                new (&buffer_[i]) Cell();
                buffer_[i].seq.store(i, std::memory_order_relaxed);
            }

            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_ = 0;
        }


        ~SingleQueue() noexcept {
            for (size_t i = 0; i < size_; ++i) buffer_[i].~Cell();
            xq::utils::free(buffer_);
        }


        bool
        empty() const noexcept {
            size_t eq = enqueue_pos_.load(std::memory_order_acquire);
            return eq == dequeue_pos_;
        }


        bool
        enqueue(T&& data) noexcept {
            size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
            int spin = 1;

            for (;;) {
                Cell* cell = &buffer_[pos & mask_];
                size_t seq = cell->seq.load(std::memory_order_acquire);
                intptr_t diff = (intptr_t)seq - (intptr_t)pos;

                if (diff == 0) {
                    if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                        cell->data = std::move(data);
                        cell->seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                } else if (diff < 0) {
                    // 满了
                    return false; 
                } else {
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }

                // Backoff 优化
                for (int i = 0; i < spin; ++i) {
                    ::_mm_pause();
                }

                if (spin < 16) {
                    spin <<= 1;
                } else {
                    std::this_thread::yield();
                }
            }
        }


        size_t
        try_dequeue_bulk(T* out, size_t max_n) noexcept {
            size_t count = 0;
            size_t pos = dequeue_pos_;
            while (count < max_n) {
                Cell* cell = &buffer_[pos & mask_];
                size_t seq = cell->seq.load(std::memory_order_acquire);
                if ((intptr_t)seq - (intptr_t)(pos + 1) == 0) {
                    out[count++] = std::move(cell->data);
                    cell->seq.store(pos + size_, std::memory_order_release);
                    ++pos;
                } else { 
                    break; 
                }
            }

            dequeue_pos_ = pos;
            return count;
        }


    private:
        const size_t size_;
        const size_t mask_;
        Cell* const buffer_;
        alignas(64) std::atomic<size_t> enqueue_pos_;
        alignas(64) size_t dequeue_pos_;
    }; // class SingleQueue;


public:
    /**
     * @param shard_count 分片数, 必须是 2 的幂(如 4, 8, 16)
     * @param per_shard_size 每个分片的容量, 必须是 2 的幂
     */
    explicit MPSC(size_t shard_count, size_t per_shard_size) noexcept
        : shard_mask_(shard_count - 1) {
        ASSERT(shard_count > 0 && (shard_count & (shard_count - 1)) == 0, "MPSC 的 shard_count 必须是 2 的幂次方");
        ASSERT(per_shard_size > 0 && (per_shard_size & (per_shard_size - 1)) == 0, "MPSC 的 per_shard_size 必须是 2 的幂次方");
        for (size_t i = 0; i < shard_count; ++i) {
            shards_.emplace_back(std::make_unique<SingleQueue>(per_shard_size));
        }
    }


    bool
    empty() const noexcept {
        for (size_t i = 0; i < shards_.size(); ++i) {
            if (!shards_[i]->empty()) {
                return false;
            }
        }
        return true;
    }


    bool
    enqueue(T&& data) noexcept {
        thread_local static size_t cached_idx = []() {
            size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return h;
        }();

        size_t idx = cached_idx & shard_mask_;
        
        if (shards_[idx]->enqueue(std::move(data))) {
            return true;
        }

        for (size_t i = 1; i <= shard_mask_; ++i) {
            if (shards_[(idx + i) & shard_mask_]->enqueue(std::move(data))) {
                return true;
            }
        }

        return false;
    }


    size_t
    try_dequeue_bulk(T* out, size_t max_n) noexcept {
        size_t total = 0;
        for (size_t i = 0; i < shards_.size(); ++i) {
            total += shards_[i]->try_dequeue_bulk(out + total, max_n - total);
            if (total >= max_n) {
                break;
            }
        }

        return total;
    }


    void
    clear(std::function<void(T&)> handle) {
        int n;
        T es[16];
        while ((n = try_dequeue_bulk(es, 16)) > 0) {
            for (int i = 0; i < n; ++i) {
                handle(es[i]);
            }
        }
    }


private:
    const size_t shard_mask_;
    std::vector<std::unique_ptr<SingleQueue>> shards_;
}; // class MPSC;


} // namespace xq::utils;


#endif // __XQ_UTILS_MPSC_HPP__