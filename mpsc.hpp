#include <atomic>
#include <cstddef>
#include <new>
#include <algorithm>
#include <immintrin.h> // _mm_pause
#include <vector>
#include <memory>
#include <thread>

/**
 * HyperMPSCQueue: 专为不绑核环境优化的多分片 MPSC 队列
 * 核心逻辑：通过多个独立的子队列分摊竞争，即使某个生产者被 OS 挂起，
 * 消费者依然可以处理其他分片的数据，避免整体停顿。
 */
template<typename T>
class HyperMPSCQueue {
private:
    // --- 内部基础组件：极致优化的单队列 ---
    class SingleMPSCQueue {
    public:
        struct alignas(64) Cell {
            std::atomic<size_t> seq;
            T data;
        };

        explicit SingleMPSCQueue(size_t size)
            : size_(size), mask_(size - 1),
              buffer_(static_cast<Cell*>(::operator new[](sizeof(Cell) * size))) {
            for (size_t i = 0; i < size_; ++i) {
                new (&buffer_[i]) Cell();
                buffer_[i].seq.store(i, std::memory_order_relaxed);
            }
            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_ = 0;
        }

        ~SingleMPSCQueue() {
            for (size_t i = 0; i < size_; ++i) buffer_[i].~Cell();
            ::operator delete[](buffer_);
        }

        bool enqueue(T&& data) {
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
                    return false; // 满了
                } else {
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }

                // Backoff 优化
                for (int i = 0; i < spin; ++i) _mm_pause();
                if (spin < 16) spin <<= 1;
                else std::this_thread::yield(); // 不绑核时，长时间竞争应让出 CPU
            }
        }

        size_t dequeue_bulk(T* out, size_t max_n) {
            size_t count = 0;
            size_t pos = dequeue_pos_;
            while (count < max_n) {
                Cell* cell = &buffer_[pos & mask_];
                size_t seq = cell->seq.load(std::memory_order_acquire);
                if ((intptr_t)seq - (intptr_t)(pos + 1) == 0) {
                    out[count++] = std::move(cell->data);
                    cell->seq.store(pos + size_, std::memory_order_release);
                    ++pos;
                } else { break; }
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
    };

public:
    /**
     * @param shard_count 分片数，必须是 2 的幂（如 4, 8, 16）
     * @param per_shard_size 每个分片的容量，必须是 2 的幂
     */
    explicit HyperMPSCQueue(size_t shard_count, size_t per_shard_size)
        : shard_mask_(shard_count - 1) {
        for (size_t i = 0; i < shard_count; ++i) {
            shards_.emplace_back(std::make_unique<SingleMPSCQueue>(per_shard_size));
        }
    }

    // 生产者调用
    bool enqueue(T&& data) {
        // 利用线程 ID 散列，让生产者分布在不同 shard
        thread_local static size_t tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        size_t idx = tid_hash & shard_mask_;
        
        if (shards_[idx]->enqueue(std::move(data))) return true;

        // 如果首选 shard 满，尝试探测其他 shard
        for (size_t i = 1; i <= shard_mask_; ++i) {
            if (shards_[(idx + i) & shard_mask_]->enqueue(std::move(data))) return true;
        }
        return false;
    }

    // 消费者调用 (Reactor 线程)
    size_t dequeue_bulk(T* out, size_t max_n) {
        size_t total = 0;
        for (size_t i = 0; i < shards_.size(); ++i) {
            total += shards_[i]->dequeue_bulk(out + total, max_n - total);
            if (total >= max_n) break;
        }
        return total;
    }

private:
    std::vector<std::unique_ptr<SingleMPSCQueue>> shards_;
    const size_t shard_mask_;
};
