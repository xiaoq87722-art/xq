# 开发计划

## 已完成
- [x] Acceptor + Reactor + Session 连接管理
- [x] 超时踢人
- [x] 广播
- [x] Echo bench client（examples/client）
- [x] Echo server（examples/server）

---

## 开发顺序

### Step 1 — Connector
业务服主动连接网关的客户端连接管理，对应 Acceptor 的客户端版本。

**功能：**
- 单 Connector 线程 + 复用现有 Reactor 线程
- 非阻塞 connect + EPOLLOUT 检测连接完成
- 断线自动重连（带退避策略）
- 动态增减连接目标（为后续服务发现预留接口）

**验证方式：**
- examples/client 加一个基于 Connector 的功能示例（与 bench client 并列）
- bench client 保持独立，不替换

---

### Step 2 — 协议层
TCP 是流式协议，需要消息边界处理（拆包/粘包）。

**消息格式：**
```
[4字节 消息长度][4字节 消息ID][消息体]
```

**功能：**
- 固定消息头解析
- 不完整消息缓存，凑够一条再回调
- 集成到 IService::on_data 上层，对业务透明

---

### Step 3 — 异步消息框架
游戏场景以单向推送为主，核心是按消息 ID 分发到对应处理函数。

**功能：**
- 消息 ID → 处理函数注册/分发
- 可选的请求-响应匹配（call_id + 超时），用于少数需要等回包的场景
- 序列化方案（Protobuf 或自定义）

---

### Step 4 — 网关↔业务服多对多路由
**核心问题：** 业务服给某个玩家推消息，必须路由到该玩家所在的网关。

**方案：**
- 业务服通过 etcd 发现所有网关，并与每个网关建立长连接（用 Connector）
- 网关维护 玩家ID → Session 映射
- 业务服消息带目标玩家ID，网关收到后找到对应 Session 转发

---

### Step 5 — 服务注册与发现
- 网关启动时注册到 etcd
- 业务服 watch etcd，动态增减与网关的连接
- 网关下线时业务服自动断开并清理路由

---

## 架构概览

```
玩家 ──TCP──▶ Gateway（Acceptor + Reactor）
                  ▲
                  │ TCP（业务服主动连，Connector）
                  │
             Business Service
             （Connector + Reactor）
                  │
                 etcd（服务发现）
```
