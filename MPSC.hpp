#include <atomic>
#include <cstddef>
#include <new>
#include <algorithm>
#include <immintrin.h> 
#include <vector>
#include <memory>

// 保持你原有的极致单队列实现，但加入 yield 优化
template<typename T>
class MPSCQueue {
    // ... (此处为你提供的 MPSCQueue 源码，建议在 enqueue 的 backoff 里加入 yield)
    // 优化：在 spin 达到上限后使用 std::this_thread::yield()，防止在不绑核时空耗 CPU
};

/**
 * ShardedMPSCQueue: 
 * 针对“不绑核”环境优化的 MPSC 容器。
 * 通过内部维护多个 MPSCQueue 分片，降低单点竞争和队头阻塞风险。
 */
template<typename T>
class ShardedMPSCQueue {
public:
    // shard_count 必须是 2 的幂，建议设置为核心数或 4/8
    explicit ShardedMPSCQueue(size_t shard_count, size_t per_shard_size)
        : shard_mask_(shard_count - 1) {
        for (size_t i = 0; i < shard_count; ++i) {
            shards_.emplace_back(std::make_unique<MPSCQueue<T>>(per_shard_size));
        }
    }

    // 生产者分流：利用线程本地 ID 降低冲突
    bool enqueue(T&& data) {
        // 获取一个线程相关的索引，thread_local 保证了分发路径的廉价
        thread_local static size_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        
        size_t idx = thread_id & shard_mask_;
        // 尝试首选分片
        if (shards_[idx]->enqueue(std::move(data))) return true;

        // 首选分片满，尝试遍历其他分片（Back-up plan）
        for (size_t i = 1; i <= shard_mask_; ++i) {
            if (shards_[(idx + i) & shard_mask_]->enqueue(std::move(data))) return true;
        }
        return false;
    }

    // 消费者批量抓取：轮询所有分片
    size_t dequeue_bulk(T* out, size_t max_n) {
        size_t total = 0;
        // 消费者依次检查每个分片
        for (size_t i = 0; i <= shard_mask_; ++i) {
            total += shards_[i]->dequeue_bulk(out + total, max_n - total);
            if (total >= max_n) break;
        }
        return total;
    }

private:
    std::vector<std::unique_ptr<MPSCQueue<T>>> shards_;
    const size_t shard_mask_;
};
