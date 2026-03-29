#ifndef __XQ_NET_CONF_HPP__
#define __XQ_NET_CONF_HPP__


namespace xq {
namespace net {


class Conf {
public:
    static Conf*
    instance() noexcept {
        static Conf conf;
        return &conf;
    }


    /** 队列深度 */
    int
    que_depth() const {
        return que_depth_;
    }

    /** 接收缓冲区大小 */
    int
    rcv_buf() const {
        return rcv_buf_;
    }

    /** 发送缓冲区大小 */
    int
    snd_buf() const {
        return snd_buf_;
    }

    /** 读超时 */
    int
    timeout() const {
        return timeout_;
    }

    /** 单线程最大承载 */
    int
    per_max_conn() const {
        return per_max_conn_;
    }

    /** buf ring 缓冲区大小 */
    int
    br_buf_size() const {
        return br_buf_size_;
    }

    /** buf ring 缓冲区数量 */
    int
    br_buf_count() const {
        return br_buf_count_;
    }

    /** 心跳检查频率 */
    int
    hb_check_interval() const {
        return hb_check_interval_;
    }


private:
    Conf() {}


    int que_depth_         { 1024 };
    int rcv_buf_           { 1024 * 256 };
    int snd_buf_           { 1024 * 256 };
    int timeout_           { 40 };
    int per_max_conn_      { 1000 };
    int br_buf_size_       { 1024 };
    int br_buf_count_      { 16 };
    int hb_check_interval_ { 5 };
}; // class Conf;

        
} // namespace net
} // namespace xq


#endif // __XQ_NET_CONF_HPP__