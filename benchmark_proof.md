# 压测证明

## 环境信息
- 操作系统：Ubuntu 22.04（VMware 虚拟机）
- CPU：`24` 逻辑处理器
- 压测资源：`/picture.html`
- 服务端口：`9006`

## 服务启动命令
v1.0：
```bash
./server -p 9006 -l 0 -m 3 -o 0 -s 8 -t 8 -c 1 -a 0
```
- `-a 0` 表示  proactor 模式。
- `-a 1` 表示  reactor 模式。
- 本次 v1.0 压测测到的是  proactor ，不是 reactor（在本地静态资源请求中，这里proactor比reactor更优秀）。

v2.0：
```bash
./server -p 9006 -l 0 -m 3 -o 0 -s 8 -t 8 -c 1
```
- 该版本没有 `-a` 参数。

## 压测命令
```bash
wrk -t8 -c400 -d30s --latency http://127.0.0.1:9006/picture.html
```

说明：
- 两个版本都使用同一条 `wrk` 命令。
- 压测时一次只启动一个服务，分别在同一个端口 `9006` 上测试。
- 下面的输出就是这条命令的原始结果。

## 原始输出

```text
===== v1.0 =====
--- run 1
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    32.17ms    5.33ms 127.89ms   93.07%
    Req/Sec     1.56k   165.20     3.35k    83.19%
  Latency Distribution
     50%   31.62ms
     75%   33.52ms
     90%   35.58ms
     99%   45.51ms
  373256 requests in 30.10s, 141.67MB read
Requests/sec:  12401.92
Transfer/sec:      4.71MB
--- run 2
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    32.54ms    6.71ms 150.70ms   96.92%
    Req/Sec     1.55k   162.59     4.11k    87.46%
  Latency Distribution
     50%   31.90ms
     75%   33.80ms
     90%   35.69ms
     99%   41.47ms
  370080 requests in 30.10s, 140.47MB read
Requests/sec:  12296.35
Transfer/sec:      4.67MB
--- run 3
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    32.60ms    4.06ms 115.61ms   87.02%
    Req/Sec     1.54k   131.81     2.50k    83.78%
  Latency Distribution
     50%   32.32ms
     75%   34.06ms
     90%   35.83ms
     99%   40.86ms
  367608 requests in 30.10s, 139.53MB read
Requests/sec:  12212.73
Transfer/sec:      4.64MB
```

```text
===== v2.0 =====
--- run 1
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    53.54ms    3.14ms  65.04ms   77.18%
    Req/Sec     0.93k    67.46     1.58k    80.38%
  Latency Distribution
     50%   53.68ms
     75%   55.32ms
     90%   56.91ms
     99%   60.25ms
  223558 requests in 30.09s, 84.85MB read
Requests/sec:   7429.96
Transfer/sec:      2.82MB
--- run 2
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    53.17ms    4.10ms  65.92ms   79.80%
    Req/Sec     0.94k    85.62     1.94k    85.05%
  Latency Distribution
     50%   53.54ms
     75%   55.56ms
     90%   57.41ms
     99%   60.68ms
  225168 requests in 30.10s, 85.47MB read
Requests/sec:   7481.87
Transfer/sec:      2.84MB
--- run 3
Running 30s test @ http://127.0.0.1:9006/picture.html
  8 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    53.09ms    5.13ms 112.90ms   81.86%
    Req/Sec     0.94k    99.93     1.90k    84.38%
  Latency Distribution
     50%   53.40ms
     75%   55.65ms
     90%   57.63ms
     99%   63.96ms
  225503 requests in 30.10s, 85.59MB read
Requests/sec:   7492.30
Transfer/sec:      2.84MB
```

## 说明
- 以上数据来自我通过 SSH 连接到 Ubuntu 虚拟机后，实际执行 `wrk` 压测得到的原始输出。
- 输出格式是 `wrk` 的原生输出，没有手工编造或改写数值。
- 两个版本是顺序压测的，同一时间只启动一个服务，使用相同端口和相同资源。
