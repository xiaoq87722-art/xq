--- BUG ---

BUG 1: sending_ Double-Check 竞态 (session.cpp submit_send) [已修复]
  sending_.store(false, release) 后重新检查队列：
  if (!wque_.empty() && !sending_.exchange(true, acq_rel)) { submit_send(...); }

BUG 2: loaded_ 负载均衡指标错误 (reactor.hpp / acceptor.cpp)
  loaded_ 是累计字节数，只增不减。运行时间越长的 Reactor 看起来越"忙"，
  导致新连接持续涌向最新启动的 Reactor，长期运行后负载严重不均。
  修复：改用当前活跃连接数 sessions_.size() 或单独维护 atomic<int> conn_count_ 作为负载指标。

BUG 5: br_buf_count 默认值过小，高并发下必然触发 -ENOBUFS (conf.hpp / reactor.cpp)
  当前默认 br_buf_count=16，同一 Reactor 同时到来超过 16 个 recv CQE 即会耗尽。
  当前错误处理为 xFATAL 直接杀进程，不可接受。
  修复：
    (1) 按并发规模调整默认值（参考下表）：
        5K  并发 → 512,  buf_size=1024B
        10K 并发 → 1024, buf_size=1024B
        100K并发 → 4096, buf_size=1024B
        1M  并发 → 4096, buf_size=512B，同时 Reactor 数需手动设为 32~64
    (2) -ENOBUFS 时不能 FATAL，应降级处理：记录警告，重新提交不带 buf_ring 的 recv，
        或等下次循环回收 buffer 后再试。

BUG 6: 跨线程 send 失败静默丢包 (session.cpp send / reactor.cpp on_s_recv) [已修复]
  send() 改为 void，队列满时 ASSERT 崩溃，不再静默丢包。