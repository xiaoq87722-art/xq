## TODO

- **跨线程 send 去重通知**
  在 `Session` 上增加 `std::atomic<bool> notify_pending_`，跨线程 send 时用 CAS false→true 控制，只有首次入队才调用 `reactor_->post()`；`on_send` 先 `store(false)` 再 drain，避免高并发下每次跨线程 send 都写一次 eventfd 产生大量系统调用。

- **EPOLLOUT 注册状态感知**
  在 `Session` 上增加 `std::atomic<bool> epollout_registered_`，当 `sbuf_` 因 EAGAIN 已注册 EPOLLOUT 时，跨线程 send 只入队不发通知，等 EPOLLOUT 触发时统一 drain，避免 `on_send` 反复尝试发送又遇 EAGAIN 的无效开销。
