--- BUG ---

BUG 1: sending_ Double-Check 竞态 (session.cpp submit_send) [已修复]
  sending_.store(false, release) 后重新检查队列：
  if (!wque_.empty() && !sending_.exchange(true, acq_rel)) { submit_send(...); }

BUG 2: loaded_ 负载均衡指标错误 (reactor.hpp / acceptor.cpp)
  loaded_ 是累计字节数，只增不减。运行时间越长的 Reactor 看起来越"忙"，
  导致新连接持续涌向最新启动的 Reactor，长期运行后负载严重不均。
  修复：改用当前活跃连接数 sessions_.size() 或单独维护 atomic<int> conn_count_ 作为负载指标。

BUG 3:
  SndBuf 和 Buffer 的对象池