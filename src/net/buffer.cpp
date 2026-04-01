#include "xq/net/buffer.hpp"


void
xq::net::Buffer::append(const uint8_t* data, uint32_t datalen) noexcept {
    uint32_t logical_len = len();
    if (end_ + datalen > cap_) {
        if (logical_len + datalen > cap_) {
            uint32_t new_cap = std::max(cap_ * 2, logical_len + datalen);
            data_ = (uint8_t*)xq::utils::realloc(data_, new_cap);
            ASSERT(data_, "realloc failed");
            cap_ = new_cap;
        }

        if (start_ > 0 && logical_len > 0) {
            ::memmove(data_, data_ + start_, logical_len);
        }

        start_ = 0;
        end_ = logical_len;
    }

    ::memcpy(data_ + end_, data, datalen);
    end_ += datalen;
}