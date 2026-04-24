#ifndef __XQ_UTILS_SPSC_HPP__
#define __XQ_UTILS_SPSC_HPP__


#include <atomic>
#include <cstddef>
#include <cassert>
#include <immintrin.h>


namespace xq::utils {


/**
 * @brief 单生产者单消费者无锁队列
 *        生产者和消费者必须分别在固定的线程中调用，不支持多线程并发生产或消费
 *
 * @tparam T    元素类型，需支持移动语义
 * @tparam N    队列容量，必须是 2 的幂次方
 */
template<typename T, size_t N = 4096>
class SPSC {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N 必须是 2 的幂次方");

    static constexpr size_t MASK = N - 1;
    static constexpr size_t SLOT_ALIGN = 64;

    // 每个 slot 独占一条 cache line，避免生产者/消费者之间的 false sharing
    struct alignas(SLOT_ALIGN) Slot {
        T data;
    };

public:
    SPSC() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }


    ~SPSC() noexcept = default;


    SPSC(const SPSC&) = delete;
    SPSC& operator=(const SPSC&) = delete;
    SPSC(SPSC&&) = delete;
    SPSC& operator=(SPSC&&) = delete;


    /**
     * @brief 入队，仅生产者线程调用
     * @return 成功返回 true，队列满返回 false
     */
    bool
    enqueue(T&& val) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);

        if (head - cached_tail_ >= N) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= N) {
                return false;
            }
        }

        slots_[head & MASK].data = std::move(val);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }


    /**
     * @brief 出队，仅消费者线程调用
     * @return 成功返回 true，队列空返回 false
     */
    bool
    dequeue(T& val) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return false;
            }
        }

        val = std::move(slots_[tail & MASK].data);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }


    size_t
    try_dequeue_bulk(T* buf, size_t max) noexcept {
        size_t tail  = tail_.load(std::memory_order_relaxed);
        size_t head  = head_.load(std::memory_order_acquire);
        size_t count = std::min(head - tail, max);

        for (size_t i = 0; i < count; ++i) {
            buf[i] = std::move(slots_[(tail + i) & MASK].data);
        }

        tail_.store(tail + count, std::memory_order_release);
        return count;
    }


    bool
    empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }


    void
    clear(std::function<void(T&)> handle) noexcept {
        int n;
        T es[16];
        while ((n = try_dequeue_bulk(es, 16)) > 0) {
            for (int i = 0; i < n; ++i) {
                handle(es[i]);
            }
        }
    }


private:
    Slot slots_[N];

    // 生产者 cache line：head_ 与 cached_tail_ 同行，producer 访问时只需一次 cache load
    alignas(64) std::atomic<size_t> head_;  // 生产者写，消费者读
    size_t cached_tail_ { 0 };              // 仅生产者访问

    // 消费者 cache line：tail_ 与 cached_head_ 同行
    alignas(64) std::atomic<size_t> tail_;  // 消费者写，生产者读
    size_t cached_head_ { 0 };              // 仅消费者访问
}; // class SPSC;


} // namespace xq::utils


#endif // __XQ_UTILS_SPSC_HPP__