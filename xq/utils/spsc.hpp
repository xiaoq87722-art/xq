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
template<typename T, size_t N>
class SPSC {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N 必须是 2 的幂次方");

    static constexpr size_t MASK = N - 1;

    struct Slot {
        std::atomic<size_t> seq { 0 };
        T data;
    };

public:
    SPSC() noexcept {
        for (size_t i = 0; i < N; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
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
        Slot& slot  = slots_[head & MASK];
        size_t seq  = slot.seq.load(std::memory_order_acquire);

        if (seq != head) {
            // 队列满
            return false;
        }

        slot.data = std::move(val);
        slot.seq.store(head + 1, std::memory_order_release);
        head_.store(head + 1, std::memory_order_relaxed);
        return true;
    }


    /**
     * @brief 出队，仅消费者线程调用
     * @return 成功返回 true，队列空返回 false
     */
    bool
    dequeue(T& val) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& slot  = slots_[tail & MASK];
        size_t seq  = slot.seq.load(std::memory_order_acquire);

        if (seq != tail + 1) {
            // 队列空
            return false;
        }

        val = std::move(slot.data);
        slot.seq.store(tail + N, std::memory_order_release);
        tail_.store(tail + 1, std::memory_order_relaxed);
        return true;
    }


    bool
    empty() const noexcept {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t seq  = slots_[tail & MASK].seq.load(std::memory_order_acquire);
        return seq != tail + 1;
    }

private:
    Slot slots_[N];

    alignas(64) std::atomic<size_t> head_;  // 生产者写
    alignas(64) std::atomic<size_t> tail_;  // 消费者写
}; // class SPSC;


} // namespace xq::utils


#endif // __XQ_UTILS_SPSC_HPP__