# TODO: 将 libuv 替换为裸 epoll

## 1. Acceptor — 监听套接字（简单）

- 删除 `uv_poll_t poll_handle_`，改用 `epoll_ctl` 直接监听 `fd_`
- `Listener::start()` 中用 `epoll_ctl(EPOLL_CTL_ADD, fd_, EPOLLIN)` 替换 `uv_poll_init/uv_poll_start`
- `Listener::stop()` 中用 `epoll_ctl(EPOLL_CTL_DEL)` + `::close(fd_)` 替换 `uv_close`
- `Acceptor::run()` 中主循环改为 `epoll_wait`，替换 `uv_run`

## 2. 信号处理（简单）

- 删除 `uv_signal_t sigint, sigterm`
- 用 `signalfd` 替换：
  ```cpp
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, nullptr);
  int sfd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  // 注册到 acceptor 的 epoll
  ```

## 3. Reactor — 跨线程唤醒（简单）

- 删除 `uv_async_t* async_`，改用 `int wakeup_fd_`（eventfd）
- `Reactor::run()` 初始化：
  ```cpp
  wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeup_fd_, EPOLLIN);
  ```
- `Reactor::post()` / `stop()` 中唤醒：
  ```cpp
  uint64_t val = 1;
  ::write(wakeup_fd_, &val, 8);
  ```
- `epoll_wait` 返回 `wakeup_fd_` 可读时，读掉数据再处理 MPSC 队列：
  ```cpp
  uint64_t val;
  ::read(wakeup_fd_, &val, 8);
  ```

## 4. Reactor — 读取（简单）

- 删除 `uv_tcp_t`，Session 直接持有 `SOCKET fd_`
- 删除 `uv_read_start / on_read_alloc`
- `epoll_wait` 返回 `EPOLLIN` 时直接 `::read(fd_, s->rbuf(), RBUF_MAX)`
- 读取结果处理逻辑与现在的 `on_read` 相同

## 5. Reactor — 发送队列（复杂，核心改动）

libuv 的 `uv_write` 内部处理了 EAGAIN 和半发送，改为裸 epoll 后需要自己实现。

### Session 需要新增
- `std::deque<WriteBuf*> send_queue_` 待发送队列
- `size_t send_offset_` 当前 WriteBuf 已发送的字节数
- `bool writing_` 是否已注册 EPOLLOUT

### 发送逻辑
```
send() 调用时：
  → 将 WriteBuf 追加到 send_queue_
  → 如果 !writing_，尝试立即 ::write()
      → 发完：WriteBuf 归还池，继续下一个
      → EAGAIN：注册 EPOLLOUT，writing_ = true
      → 半发送：更新 send_offset_，注册 EPOLLOUT

epoll_wait 返回 EPOLLOUT 时：
  → 继续发送 send_queue_ 队头
  → 全部发完：epoll_ctl 取消 EPOLLOUT，writing_ = false
```

### 连接关闭时
- 遍历 `send_queue_`，将所有未发完的 WriteBuf 归还池
- `epoll_ctl(EPOLL_CTL_DEL, fd_)`
- `::close(fd_)`

## 6. 清理

- 删除 `#include <uv.h>` 及所有 libuv 相关头文件
- 删除 `uv_loop_t`、`uv_tcp_t`、`uv_handle_t` 等类型依赖
- CMakeLists.txt 中移除 libuv 链接
