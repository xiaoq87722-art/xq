#include <atomic>
#include <vector>
#include <cstddef>
#include <cassert>

template<typename T>
class MPSCRingBuffer {
public:
    explicit MPSCRingBuffer(size_t size)
        : size_(round_up_pow2(size)),
          mask_(size_ - 1),
          buffer_(size_)
    {
        for (size_t i = 0; i < size_; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_ = 0;
    }

    // 多生产者调用
    bool enqueue(const T& data) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // 队列满
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = data;
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    // 单消费者调用
    bool dequeue(T& data) {
        Cell* cell;
        size_t pos = dequeue_pos_;

        cell = &buffer_[pos & mask_];
        size_t seq = cell->seq.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

        if (diff == 0) {
            dequeue_pos_ = pos + 1;
            data = cell->data;
            cell->seq.store(pos + size_, std::memory_order_release);
            return true;
        }

        return false; // 队列空
    }

private:
    struct Cell {
        std::atomic<size_t> seq;
        T data;
    };

    size_t round_up_pow2(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

private:
    size_t size_;
    size_t mask_;
    std::vector<Cell> buffer_;

    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) size_t dequeue_pos_;
};