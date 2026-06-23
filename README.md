# WebServer 版本对比仓库

这是一个用于对比两个 Linux WebServer 版本的仓库。仓库保留了原版实现、优化版实现，以及完整的压测证据和架构说明

## 仓库内容

- `WebServer-1.0/`：原版项目，支持 `proactor` / `reactor` 两种模式。
- `WebServer-2.0/`：优化版项目，改成主从 `reactor`，并同步优化了定时器、日志和部分并发路径。
- `docs/`：架构说明、压测汇总。
- `benchmark_proof.md`：原始压测命令、启动命令和 `wrk` 输出。

## 版本说明

| 版本 | 模式 | 说明 |
|---|---|---|
| v1.0 | `proactor` / `reactor` | 教学原版；本仓库压测记录里使用的是 `-a 0`，也就是 `proactor`。 |
| v2.0 | 主从 `reactor` | 使用 `SubReactor + eventfd + std::thread`，并调整了定时器、日志和部分共享数据结构。 |

## 主要优化点

- 主从 `reactor`：主线程负责接入连接，`SubReactor` 负责事件分发。
- 定时器：改成 `std::priority_queue` 最小堆 + 懒删除。
- 日志：使用 `std::filesystem` 组织路径，配合异步写线程和批量刷盘。
- 并发控制：`http_conn::m_user_count` 改为 `std::atomic<int>`。
- 连接归属：每个连接保存自己的 `epollfd`，关闭时按归属清理。
- 线程池：去掉 `actor_model` 分支，只保留 `append_p()` / `process()` 路径。
-（虽然优化是优化过了，听起来很唬人，实际上优化后的版本对本地静态资源的请求还不如原版呢，我之后会解释）

## 构建

需要 Linux / Ubuntu 环境，以及 `g++`、`make`、MySQL 客户端库。

```bash
cd WebServer-1.0
make -j

cd ../WebServer-2.0
make -j
```

> 数据库账号和库名写在各版本的 `main.cpp` 里，按自己的环境修改。

## 启动

v1.0：

```bash
./server -p 9006 -l 0 -m 3 -o 0 -s 8 -t 8 -c 1 -a 0
```

v2.0：

```bash
./server -p 9006 -l 0 -m 3 -o 0 -s 8 -t 8 -c 1
```

## 压测复现

本仓库使用的压测命令：

```bash
wrk -t8 -c400 -d30s --latency http://127.0.0.1:9006/picture.html
```

原始输出见 `benchmark_proof.md`，三次结果汇总见 `docs/benchmark_summary.md`。

### 汇总结果

| 版本 | 平均 QPS | 平均延迟 | P99 延迟 |
|---|---:|---:|---:|
| v1.0 | 12303.67 | 32.44ms | 42.61ms |
| v2.0 | 7468.04 | 53.27ms | 61.63ms |

## 文档入口

- [压测证据](benchmark_proof.md)
- [压测汇总](docs/benchmark_summary.md)
- [架构说明](docs/architecture.md)

## 目录结构

```text
.
├── WebServer-1.0
├── WebServer-2.0
├── docs
├── benchmark_proof.md
└── README.md
```

## 说明

这个仓库的目标不是只展示“优化结果”，而是保留一个可复现的对比样本：原版为什么快、优化版为什么没有跑赢、差异到底落在哪些路径上，都能通过代码和压测证据继续探讨。
