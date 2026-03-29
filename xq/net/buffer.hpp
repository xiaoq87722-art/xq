#ifndef __XQ_NET_BUFFER__
#define __XQ_NET_BUFFER__


#include <stdint.h>
#include <memory>
#include "xq/utils/memory.hpp"
#include "xq/utils/log.hpp"


namespace xq {
namespace net {


class Buffer {
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer& operator=(Buffer&&) = delete;


    static std::atomic<int>&
    count() {
        static std::atomic<int> count;
        return count;
    }


public:
    explicit Buffer() noexcept {
        data_ = (uint8_t*)xq::utils::malloc(cap_);
        ASSERT(data_, "xq::utils::malloc({}) failed", cap_);
        xINFO("已创建 {} 个 Buffer", ++count());
    }


    ~Buffer() noexcept {
        xq::utils::free(data_);
        xINFO("还有 {} 个 Buffer 未销毁", --count());
    }


    const uint8_t*
    data() const {
        return data_ + start_;
    }


    uint32_t
    len() const {
        return end_ - start_;
    }


    uint32_t
    cap() const {
        return cap_;
    }


    void
    append(const uint8_t* data, uint32_t datalen) noexcept {
        auto nleft = cap() - len();
        if (datalen > nleft) {
            cap_ += datalen;
            auto tmp = (uint8_t*)xq::utils::realloc(data_, cap_);
            ASSERT(tmp, "realloc failed");
            data_ = tmp;
        }

        ::memcpy(data_ + end_, data, datalen);
        end_ += datalen;
    }


    void
    set_data(const uint8_t* data, uint32_t datalen) noexcept {
        ::memcpy(data_, data, datalen);
        end_ = datalen;
        start_ = 0;
    }


    void
    consume(uint32_t n) noexcept {
        start_ += n;
        if (start_ == end_) {
            reset();
        }
    }


    void
    reset() noexcept {
        start_ = end_ = 0;
    }


private:
    /** 数据指针 */
    uint8_t* data_ { nullptr };

    /** 数据开始位置 */
    uint32_t start_ { 0 };

    /** 数据终止位置 */
    uint32_t end_ { 0 };

    /** data_ 总大小 */
    uint32_t cap_ { 1024 };
}; // class Buffer;

    
} // namespace net
} // namespace xq


#endif // __XQ_NET_BUFFER__