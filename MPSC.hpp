#include <atomic>
#include <vector>
#include <cstddef>
#include <new>

template<typename T>
class OptimizedMPSCRingBuffer {
public:
    // size 必须是 2 的幂
    explicit OptimizedMPSCRingBuffer(size_t size)
        : size_(round_up_pow2(size)),
          mask_(size_ - 1),
          buffer_(static_cast<Cell*>(::operator new[](sizeof(Cell) * size_))) 
    {
        for (size_t i = 0; i < size_; ++i) {
            new (&buffer_[i]) Cell();
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~OptimizedMPSCRingBuffer() {
        for (size_t i = 0; i < size_; ++i) {
            buffer_[i].~Cell();
        }
        ::operator delete[](buffer_);
    }

    // 生产者 (Multi-Producer)
    bool enqueue(T&& data) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;

            if (diff == 0) {
                // 成功抢占位置后，pos 会自动递增
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
                // CAS 失败时，pos 已经被更新为最新的 enqueue_pos_，直接进入下一轮 diff 计算
            } else if (diff < 0) {
                return false; // 队列满
            } else {
                // 此时说明该位置已被其他生产者抢先或正被处理
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(data);
        // 使用 release 确保消费者看到 data 的写入
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    // 消费者 (Single-Consumer) - 极简逻辑
    bool dequeue(T& data) {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell* cell = &buffer_[pos & mask_];
        size_t seq = cell->seq.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

        if (diff == 0) {
            // 单消费者不需要 CAS，直接移动
            dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
            data = std::move(cell->data);
            // 释放位置，让生产者可以再次使用 pos + size_
            cell->seq.store(pos + size_, std::memory_order_release);
            return true;
        }

        return false; // 队列空
    }

private:
    struct alignas(64) Cell {
        std::atomic<size_t> seq;
        T data;
    };

    size_t round_up_pow2(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    const size_t size_;
    const size_t mask_;
    Cell* const buffer_;

    // 关键：物理隔离生产者和消费者的计数器
    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) std::atomic<size_t> dequeue_pos_;
};
