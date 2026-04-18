#ifndef __XQ_NET_CONF_HPP__
#define __XQ_NET_CONF_HPP__


namespace xq::net {


class Conf {
public:
    static Conf*
    instance() noexcept {
        static Conf conf;
        return &conf;
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


    /** 心跳检查频率 */
    int
    hb_check_interval() const {
        return hb_check_interval_;
    }


private:
    Conf() {}


    int rcv_buf_           { 1024 * 256 };
    int snd_buf_           { 1024 * 256 };
    int timeout_           { 40 };
    int per_max_conn_      { 1000 };
    int hb_check_interval_ { 5000 };
}; // class Conf;

        
} // namespace xq::net


#endif // __XQ_NET_CONF_HPP__