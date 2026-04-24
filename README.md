# xq

面向游戏后端的 Linux C++20 高性能 TCP 网络库。

> **当前处于开发阶段，尚不可用。** 服务端（Acceptor + Reactor）已跑通 echo，业务端（Connector + Sender + Processor）框架就位但业务 loop 未接入。

## 定位

只面向 **游戏后端** 的"玩家 → 网关 → 业务服"两段式架构。不是通用 RPC，不跨平台，不打算做成所有人都能塞进任何项目的那种库——设计上把很多决策直接针对游戏场景做了取舍（长连接、单向推送为主、消息频繁但单条不大、连接数网关侧十万级业务侧千级）。

## 架构

两端角色并不完全对称，拿出来各看各的。

### 服务端（接受玩家连接）

```
Listener (per listening port)
     │
     ▼  accept
Acceptor (单线程, 只 accept, round-robin 分发)
     │
     ▼
Reactor × N  (每线程一个 epoll, IO + 业务 同一线程)
     │
     ▼
Session  (连接对象, 池化按 fd 索引)
```

- **Listener**：一个监听地址的容器（fd + 绑定的 `IListnerEvent` + host）
- **Acceptor**：单例单线程，只做 accept，新 fd 轮询派发给某个 Reactor
- **Reactor**：线程数 = `hardware_concurrency - 2`，**读 + on_data + 写** 全在同一线程完成，避免跨线程协调
- **Session**：从 `Session* sessions_[100000]` 对象池按 fd 取出，`init/release` 管理生命周期

### 业务端（业务服主动连网关）

```
Connector (单线程, 只听 EPOLLIN)
     │ recv + 按 fd 分发
     ▼
Processor × N  (业务线程池)
     │ 业务处理后调 Conn::send
     ▼
Sender (单线程, 只听 EPOLLOUT)
     │
     ▼
Conn  (连接对象, 池化)
```

- **Connector**：单例线程，epoll 只监听读事件（从服务端收数据），收到后按 fd 分发到某个 Processor
- **Processor**：业务线程池，线程数 = `hardware_concurrency - 3`，跑 `IConnEvent::on_data`
- **Sender**：独立线程，epoll 专职处理 EPOLLOUT（写路径的 EAGAIN 重试）
- **Conn**：从 `Conn* conns_[1024]` 对象池取出

### 两端为什么不完全对称

| 角色 | 服务端 | 业务端 |
|---|---|---|
| 业务线程 | Reactor (同时兼 IO) | Processor |
| 读 IO | Reactor | Connector |
| 写 IO | Reactor | Sender（独立） |
| 接入/连接专用线程 | Acceptor | Connector |

服务端把 IO 和业务揉在同一个 Reactor 线程里——单 Session 的生命周期全程单线程，最省协调。业务端拆成读/写/业务三段：

- Conn 数量少（千级 vs 网关的十万级），给业务一个独立线程池能充分用多核跑解包/路由/状态机
- 读写分到两个 epoll 线程，避免大消息 writev 把读阻塞住

## 关键设计

- **无锁原语自造**：`MPSC`（分片 Vyukov）、`SPSC`、`RingBuf`（cacheline 隔离 head/tail）——线程间数据通路都不用锁
- **零拷贝 IO**：
  - 读：`readv` 直接写入 `RingBuf` 的 iovec 两段
  - 写：`writev` 一次聚合 `[sbuf 残留] + [sque 批量 16] + [当前 data]`，未发完的尾巴吸回 sbuf，下次 EPOLLOUT 接着冲
- **SendBuf SBO**：512 字节以内 inline 存储，跨线程 send 热路径零 malloc
- **唤醒合并**：`sending_` / `processing_` 原子 bool CAS，多次 enqueue 合并为一次 eventfd 唤醒
- **全 EPOLLET**：配合 "读到 EAGAIN / 写到 EAGAIN" 语义
- **对象池 + 在线 init/release**：Session 和 Conn 都不销毁不重建

## 服务契约

用户实现两个接口之一：

```cpp
// 服务端
class IListnerEvent {
    virtual void on_start(Listener*) = 0;
    virtual void on_stop(Listener*) = 0;
    virtual int  on_connected(Session*) = 0;
    virtual void on_disconnected(Session*) = 0;
    virtual int  on_data(Session*, xq::utils::RingBuf& rbuf) = 0;
};

// 业务端
class IConnEvent {
    virtual int  on_connected(Conn*) = 0;
    virtual void on_disconnected(Conn*) = 0;
    virtual int  on_data(Conn*, const char* data, size_t len) = 0;
};
```

`on_data` 把 RingBuf 或裸指针直接交给 service——**框架不解析协议边界**。拆包/粘包由上层实现，这是明确的设计选择。

## 依赖

- Linux 6.0+（epoll、eventfd、readv/writev）
- g++ 13+（C++20）
- [spdlog](https://github.com/gabime/spdlog)
- [mimalloc](https://github.com/microsoft/mimalloc)
- （可选）[gperftools](https://github.com/gperftools/gperftools) —— debug 构建出火焰图

不跨平台，不计划支持 Windows / macOS。

## 构建

```bash
# 根目录：出静态库（已打包 spdlog + mimalloc）
make                  # libxq.a          (release, -O2 -DNDEBUG)
make debug            # libxq_debug.a    (debug,   -O0 -g3)

# 示例
cd examples/server && make        # release
cd examples/server && make debug  # 带 gperftools, 生成 server.prof
cd examples/client && make
```

## 代码布局

```
xq/            公开 header
├── net/       Acceptor / Reactor / Session / Listener
│              Connector / Sender / Processor / Conn
│              Event / Conf
└── utils/     MPSC / SPSC / RingBuf (+ SendBuf)
               Log / Memory / Signal / Time

src/           实现
├── net/
└── utils/

examples/
├── server/    基于 IListnerEvent 的 echo server
└── client/    多线程 epoll 压测 (纯裸 epoll, 不用 xq)
```

## 示例：echo server

```cpp
#include "xq/xq.h"

class EchoService : public xq::net::IListnerEvent {
    void on_start(xq::net::Listener* l)        override {}
    void on_stop(xq::net::Listener* l)         override {}
    int  on_connected(xq::net::Session* s)     override { return 0; }
    void on_disconnected(xq::net::Session* s)  override {}

    int on_data(xq::net::Session* s, xq::utils::RingBuf& rbuf) override {
        iovec iov[2];
        int niov = rbuf.read_iov(iov);
        size_t total = 0;
        for (int i = 0; i < niov; ++i) {
            s->send((char*)iov[i].iov_base, iov[i].iov_len);
            total += iov[i].iov_len;
        }
        rbuf.read_consume(total);
        return 0;
    }
};

int main() {
    EchoService echo;
    auto l = xq::net::Listener(&echo, "0.0.0.0", 8888);
    xq::net::Acceptor::instance()->run({ &l });
    return 0;
}
```

完整可跑版本见 [examples/server/main.cpp](examples/server/main.cpp)。

## 已知限制

- **仅 Linux**，且依赖 `readv` / `writev` / `eventfd`
- **业务端业务 loop 尚未完整接入**，Connector/Sender/Processor 能编译能启动但没 end-to-end 示例
- **跨线程 `Session::send` / `Conn::send` 存在池复用 UAR race**（代码内有 TODO 标注，临时对策：同线程调用）
