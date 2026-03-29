#include <atomic>
#include <cstddef>
#include <new>
#include <algorithm>
#include <immintrin.h> // _mm_pause
#include <vector>

/**
 * Optimized MPSC (Multi-Producer Single-Consumer) Bounded Queue
 * 优化点：
 * 1. 缓存行填充 (alignas(64)) 防止伪共享。
 * 2. 指数退避 (Exponential Backoff) 缓解高并发 CAS 竞争。
 * 3. 批量出队 (Bulk Dequeue) 提升消费者吞吐。
 * 4. 优化 CAS 失败路径，利用 compare_exchange_weak 的特性。
 */
template<typename T>
class MPSCQueue {
public:
    explicit MPSCQueue(size_t size)
        : size_(round_up_pow2(size)),
          mask_(size_ - 1),
          buffer_(static_cast<Cell*>(::operator new[](sizeof(Cell) * size_)))
    {
        for (size_t i = 0; i < size_; ++i) {
            new (&buffer_[i]) Cell();
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_ = 0;
    }

    ~MPSCQueue() {
        for (size_t i = 0; i < size_; ++i) {
            buffer_[i].~Cell();
        }
        ::operator delete[](buffer_);
    }

    // 禁止拷贝
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    /**
     * 多生产者入队
     */
    bool enqueue(T&& data) {
        Cell* cell;
        // 使用 relaxed load 获取初始位置
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        int spin = 1;

        for (;;) {
            cell = &buffer_[pos & mask_];
            // 获取当前 cell 的序列号，确保该位置可写
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;

            if (diff == 0) {
                // 尝试抢占 enqueue 位置
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    // 成功抢占位置，跳出循环执行写入
                    break;
                }
                // 抢占失败时，pos 已经被 compare_exchange_weak 更新为最新的全局位置
            } else if (diff < 0) {
                // 队列已满 (seq 为上一次被消费后的状态)
                return false;
            } else {
                // 此时 diff > 0，说明其他线程抢走了当前 cell，重刷 pos
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }

            // 高并发下的退避策略：减少对 L1 Cache Line 的无效争抢
            for (int i = 0; i < spin; ++i) {
                _mm_pause();
            }
            // 快速增长 spin，但上限不要太大，保持低延迟
            spin = (spin < 32) ? (spin << 1) : 32;
        }

        cell->data = std::move(data);
        // 使用 release 确保消费者能看到移动后的 data
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * 单消费者出队
     */
    bool dequeue(T& out) {
        size_t pos = dequeue_pos_;
        Cell* cell = &buffer_[pos & mask_];
        size_t seq = cell->seq.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

        if (diff == 0) {
            dequeue_pos_ = pos + 1;
            out = std::move(cell->data);
            // 释放 cell，将序列号设为 pos + size_，允许生产者在下一轮使用
            cell->seq.store(pos + size_, std::memory_order_release);
            return true;
        }

        return false;
    }

    /**
     * 单消费者批量出队 (极致吞吐建议使用此方法)
     */
    size_t dequeue_bulk(T* out, size_t max_n) {
        size_t count = 0;
        size_t pos = dequeue_pos_;

        while (count < max_n) {
            Cell* cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

            if (diff == 0) {
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
    // 每个 Cell 占用一个 Cache Line，彻底杜绝生产者与消费者、生产者与生产者之间的 False Sharing
    struct alignas(64) Cell {
        std::atomic<size_t> seq;
        T data;
    };

    static size_t round_up_pow2(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

private:
    const size_t size_;
    const size_t mask_;
    Cell* const buffer_;

    // 将生产者指针和消费者指针物理隔开，防止它们互相污染对方的缓存行
    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) size_t dequeue_pos_; 
};

