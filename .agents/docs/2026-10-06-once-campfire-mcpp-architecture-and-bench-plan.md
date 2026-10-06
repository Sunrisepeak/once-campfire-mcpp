# once-campfire-mcpp 整体方案：C++ Modules 实现与性能架构

状态：草案（待 review，评审通过后本记录落地即不可变，后续只追加状态行或具名带日期的更正块）。
日期：2026-10-06。输入：basecamp/once-campfire-rust（README、AGENTS.md、crates 布局）、
once-campfire-elixir PR #5 的 improved 基准、mcpp 2026.10.5.2、mcpp-index（pkgs 全量清单）。

**这份记录回答的问题：** 用 mcpp + C++23 Modules 重建 ONCE Campfire，
五条被测路线全部超过已发布的最快实现（Rust 版），架构怎么定、依赖从哪来、
代码怎么组织、性能从哪里来、怎么验收。

相关文档：本文是本仓库的第一份设计记录；实现落地后，`docs/**`（使用手册）与
`docs/specs/**`（语义规范）按 mcpp-docs-style 的三棵树规则另起。

---

## 一、目标与基线

基线取「同一路线上已发布的最高值」，跨 once-campfire README 表与
once-campfire-elixir PR #5 表取最大。结论：五条路线的基线全部来自
README 表的 Rust 列；PR #5 的 Rust improved 数字（ccece30）在四条路线上更低，
不构成基线，仅作为方法学交叉参照。

| 路线 | Rails | Elixir (improved, PR#5) | Rust (improved, ccece30) | **基线（必须超过）** | 设计目标（+25% 量级，未验证） |
|---|---|---|---|---|---|
| Room page | 241 | 17,237 | 26,796 | **36,260** | ≥ 45,000 |
| Messages page | 413 | 25,238 | 28,335 | **40,872** | ≥ 51,000 |
| Sidebar | 552 | 32,121 | 27,538 | **34,672** | ≥ 43,000 |
| Search | 435 | 26,790 | 25,488 | **33,299** | ≥ 41,500 |
| Post a message | 273 | 1,995 | 6,101 | **6,896** | ≥ 10,000 |

测量条件（基线的）：AMD Ryzen AI MAX+ 395，16 并发客户端，每个应用分配
4 个硬件线程。本项目的验收测量必须复现同一条件（见第七节）。

按 4 线程折算的单请求 CPU 预算（推算，不是实测）：Room 目标 45,000 req/s ÷ 4 线程
≈ 11,250 req/s/线程 ≈ 89 µs/请求；Post 目标 10,000 ÷ 4 ≈ 400 µs/请求。
缓存命中路径的预算分配：HTTP 解析 ≤ 2 µs，会话校验 ≤ 2 µs，响应组装 ≤ 5 µs，
其余留给内核拷贝与调度。

## 二、对标实现的分析（once-campfire-rust）

仓库事实（来自其 README 与 AGENTS.md，2026-10 读取）：单二进制替换
Ruby + Puma + Redis + Resque + Thruster；直接读写 Rails 版的 SQLite 数据库、
存储目录与签名/加密 cookie；前端服务器（TLS、ACME、HTTP/2、响应缓存）与
应用监听分离；模板用 Askama 编译期展开；搜索走 SQLite 全文检索；
job 为进程内队列。crates 划分：`ruby`（字符串兼容）、`rails_compat`
（签名/加密/序列化契约）、`kit`（Axum 适配层与前端服务器）、`routes`、
`db`（rusqlite + 模型 + 查询）、`richtext`（Action Text 管线）、`storage`
（Active Storage 兼容）、`cable`（Action Cable 协议 + WebSocket）、
`assets`（Propshaft 兼容）、`views`（Askama 模板）、`campfire`（控制器与装配）、
`bench`（压测器）。

对基准起决定作用的五个机制，以及本项目的对应决策：

1. **编译期模板。** Askama 把 ERB 等价模板在编译期变成 Rust 代码，零运行时
   模板解析。C++ 对应物更进一步：partial 即函数——每个模板是一个
   `void render_xxx(html::Out&, T const&)` 函数，骨架是 `string_view` 字面量的
   memcpy，数字用 `fmt::format_int`，转义在片段生成时做一次而非每次响应。
2. **页面分片缓存 + 廉价 ETag。** Rust 版缓存「page parts」并对分片哈希得到
   ETag，而不是缓存/哈希整个响应体。本项目沿用并下沉一层：分片在**消息落库时
   渲染一次**（读路径永不渲染单条消息），页面哈希 = 分片哈希的折叠，O(分片数)
   而非 O(字节数)。
3. **SQLite 读走页缓存。** Rust 版禁用 mmap、依赖 OS page cache，并在启动时
   补 `index_messages_on_room_id_and_created_at`。本项目同样 WAL + 页缓存 +
   线程本地连接 + 预编译语句缓存。
4. **进程内一切。** 无 Redis、无外部队列。本项目相同：广播与 job 全在进程内。
5. **广播压缩一次共享。** WebSocket permessage-deflate 的压缩结果对所有订阅者
   复用。本项目沿用（cable 层，P1 阶段）。

Rust 版没有做、本项目加码的三处（性能差值的预期来源，均为设计推断、未验证）：

- **读路径零分配 + 零拷贝组装。** 每请求 arena 分配器；响应不经整页拼接，
  直接 `writev` 缓存分片的 iovec + 少量动态字节。Rust 版组装页面仍经过
  字符串拼接。
- **读路径无锁。** 房间热状态为不可变快照（`RoomSnapshot`），原子指针替换，
  读侧零锁零原子重试；写侧每房间短临界区做写时复制追加。
- **会话零 DB 查询。** session→user 走带失效的短 TTL 进程内缓存；
  Rails 兼容的 cookie 校验（AES-256-GCM / HMAC）是纯 CPU 操作（AES-NI）。

## 三、总体架构

### 3.1 线程与 IO 模型

thread-per-core，共享 nothing 监听：4 个（`std::thread::hardware_concurrency`
与基准给 4 取小）事件循环线程各自 `SO_REUSEPORT` 监听同一端口，连接被内核
分流到单一线程，**读路径全程无跨线程跳转**。事件后端第一阶段用
mcpp-index 的 `chriskohlhoff.asio`（1.38.1，模块化封装，separate compilation，
epoll 后端）。16 并发客户端下每请求只有 2–3 次系统调用（read / writev，
epoll_wait 摊薄），内核侧不是瓶颈，epoll 足够支撑目标；io_uring 列为
第二阶段的可选替换项（见第十节 D2），接口上 `http::Server` 与传输解耦。

### 3.2 请求生命周期（读路径）

1. keep-alive 循环：解析 HTTP/1.1（自写解析器，字段以 `string_view` 指向
   连接缓冲，零拷贝；`\r\n` 扫描 + 少量分支预测友好的状态机）。
2. 路由：constexpr 静态 trie，编译期把已知路由展开为直接调用；
   `<room_id>` 动态段用 `std::from_chars`。
3. 会话：解析 cookie，验签（OpenSSL EVP，AES-NI），session→user 短 TTL 缓存。
4. 取 `RoomSnapshot`（`std::atomic<std::shared_ptr<const RoomSnapshot>>` 读）。
5. `If-None-Match` 与滚动页哈希比对，命中直接 304（无 body）。
6. 组装：缓存分片 iovec + 动态头部，单次 `writev`。

### 3.3 缓存分层

| 层 | 内容 | 失效 |
|---|---|---|
| `Fragment` | 单条消息/侧栏块渲染好的字节 + XXH3 哈希 + 预压缩 gzip 字节 | 消息编辑/删除/反应变更（写时替换） |
| `RoomSnapshot` | 不可变：侧栏 partial、置顶、最近 N=512 条 Fragment 的环、页面骨架、折叠页哈希 | 每次写操作后由写路径替换 |
| 会话缓存 | session 摘要 → `User`，60 s TTL + 用户更新时失效 | TTL / 主动失效 |
| 结果缓存 | (room, term) 搜索结果页，≤ 256 项 LRU | 房间有新消息即整表清空 |

ETag 是哈希树：`Fragment.hash` 在渲染时算一次；页哈希 = 折叠各分片哈希，
条件请求路径不触碰字节内容。

### 3.4 数据库与写路径

`compat.sqlite3`（3.45.3 amalgamation）。每线程一个连接；WAL、
`synchronous=NORMAL`、`mmap off`（与 Rust 版实测选择一致）、
`cache_size=-65536`、`temp_store=MEMORY`、预编译语句进程级缓存。
启动时补建 Rust 版同款索引 `messages(room_id, created_at)`。

Post 路径：解析 form → richtext 消毒并渲染成 Fragment（一次）→ 单条
prepared INSERT（WAL 写锁串行化，10,000 req/s 目标下竞争可忽略，
不需要独立写线程）→ 写时复制更新 `RoomSnapshot` → 向 cable hub 投递广播
（惰性压缩，0 订阅者时 O(1)）→ 响应。不等待 checkpoint； durability
语义与 Rails/Rust 版对齐（NORMAL）。

### 3.5 搜索

FTS5 与 Rust 版同源（SQLite 全文检索，literal term 语义）。注意：
amalgamation 需要 `-DSQLITE_ENABLE_FTS5`，索引包 `compat.sqlite3` 的描述符
是否已开启**未验证**；若未开启，按 AGENTS.md 的约定在仓库内自建
`campfire.sqlite3-fts5` 本地索引包（见第五节）。热结果走 3.3 的结果缓存。

## 四、C++ Modules 组织与 .cppm/.cpp 划分

### 4.1 模块清单

模块名按 mcpp-style-ref 的 `topdir.subdir.file` 规则：

| 模块 / 分区 | 职责 | 对标 crate |
|---|---|---|
| `campfire.config` | 配置、flags | campfire main |
| `campfire.log` | 环形缓冲异步日志（热路径无锁入队） | — |
| `campfire.http.server` | 传输无关服务器接口 + asio 实现 | kit / front |
| `campfire.http.request` / `.response` / `.router` / `.parser` | 协议层 | kit |
| `campfire.http.ws` | WebSocket 帧与 permessage-deflate | cable |
| `campfire.cable.hub` | 订阅表、广播、一次压缩共享 | cable |
| `campfire.db.sqlite` | RAII 句柄、线程本地连接、语句缓存 | db |
| `campfire.db.models` / `.queries` | 模型与查询（Rails schema 兼容） | db |
| `campfire.cache.fragment` / `.room` / `.session` | 3.3 的三层 | （Rust 版内联在 handlers） |
| `campfire.views.html` | 转义、`html::Out` 构建器 | ruby / views |
| `campfire.views.*` | layout / room_page / messages_page / sidebar / search / message 各一个渲染单元 | views（Askama） |
| `campfire.richtext` | 消毒、附件、autolink、mention、纯文本 | richtext |
| `campfire.auth.cookies` / `.session` | Rails 签名/加密 cookie 兼容 | rails_compat / kit |
| `campfire.search` | FTS5 查询与结果缓存 | db（search） |
| `campfire.jobs` | 进程内队列（concurrentqueue） | campfire jobs |
| `campfire.app` | 装配：路由表、房间快照、依赖注入 | campfire |
| `main.cpp` | 10 行量级：读配置、起 N 个核、join | — |

### 4.2 .cppm / .cpp 分离策略（分析）

判据一条：**热路径内联得越深越好，冷路径编译依赖切得越干净越好。**

- **写法 A（接口与实现同 .cppm）**用于：`views.*` 渲染函数、`parser`、
  `fragment`、`router`、会话缓存——同模块内调用方与被调方在同一编译单元，
  编译器可以跨函数内联；这些正是 89 µs 预算里的大头。
- **写法 B（.cppm 接口 + .cpp 实现）**用于：`db.sqlite`、`http.server`、
  `richtext`、`cable.hub`、`jobs`、`config`——实现藏在 .cpp 里，改动不触发
  全量 BMI 重编，调用方只付声明成本；这些模块的热路径边界收敛在少数几个
  `export` 的 inline 薄封装上（如 `Statement::step` 的句柄判空），厚的部分
  留在 .cpp。
- 跨模块热点不导出宽接口：`RoomSnapshot` 通过 `shared_ptr const&` 传递，
  消费方拿指针不拷贝；`html::Out` 是唯一可变输出汇点，其 `append`
  是模块内 inline。

风格绑定（mcpp-style-ref 全文有效）：类型 PascalCase、成员 camelCase、
函数 snake_case、私有 `_` 后缀、`{}` 初始化、只读字符串参数 `string_view`、
错误 `std::expected`/`std::optional`、无裸 new/delete、宏换 `constexpr`。
`import std;` 全量使用，不混 `#include` 标准头。

## 五、依赖与 mcpp 索引

### 5.1 来自 mcpp-index 的包

| 依赖 | 选择器（示意） | 用途 |
|---|---|---|
| Asio（独立版，模块） | `chriskohlhoff.asio = "1.38.1"` | 事件循环、socket、`co_spawn` |
| SQLite | `compat.sqlite3 = "3.45.3"` | 存储（FTS5 见 3.5 的验证项） |
| OpenSSL | `compat.openssl` | cookie AES-GCM/HMAC；后期 TLS |
| zlib | `compat.zlib` | gzip（Content-Encoding 与 304 语义对齐 Rails） |
| fmt | `fmtlib.fmt` | 整数/格式化（热路径用 `format_int`） |
| yyjson | `compat.yyjson` | bot/webhook JSON |
| concurrentqueue | `compat.concurrentqueue` | job 与广播队列 |
| mimalloc | `compat.mimalloc` | 全局分配器 + arena 底座 |
| tomlplusplus | `marzer.tomlplusplus` | 配置解析 |

选择器以 `mcpp index` 实际解析为准；上表是设计意图，不是已验证的 manifest。

### 5.2 仓库内自建（AGENTS.md「没有的自建 mcpp 索引」条款）

| 包 | 原因 | 形态 |
|---|---|---|
| `campfire.sqlite3-fts5`（若 5.1 验证为否） | FTS5 编译开关 | 描述符 = compat.sqlite3 + `-DSQLITE_ENABLE_FTS5` |
| `campfire.xxhash` | 索引无 xxhash；ETag 哈希树需要 | 单头库 + 一个锚 TU 的 Form B 描述符 |

### 5.3 mcpp.toml 骨架（示意，键名以 docs/04 为准）

```toml
[package]
name        = "once-campfire-mcpp"
version     = "0.1.0"
description = "ONCE Campfire in C++23 Modules — drop-in over the Rails data"

[build]
default-profile = "release"

[toolchain]
default = "gcc@16.1.0"          # C++23 + import std

[dependencies]
chriskohlhoff.asio         = "1.38.1"
compat.sqlite3             = "3.45.3"
compat.openssl             = "3.x"
compat.zlib                = "1.x"
fmtlib.fmt                 = "11.x"
compat.yyjson              = "0.10.x"
compat.concurrentqueue     = "1.0.5"
compat.mimalloc            = "2.x"
marzer.tomlplusplus        = "3.x"

[targets.campfire]
# 单一可执行目标；模块图由 src/ 下的 .cppm/.cpp 推导
```

## 六、逐路线的关键机制

共享热路径见 3.2。各路线在共享路径上的差异：

- **Room page**：整页 = 骨架(常量) + 侧栏 Fragment + 置顶 Fragment + 消息环
  最近页 + 尾部(常量)，全部 iovec 化；页哈希折叠后与 `If-None-Match` 比对。
  预算大头是 writev 与内核拷贝，用户态组装目标 ≤ 5 µs。
- **Messages page**：任意 page=N 由 Fragment 环按窗口切片组装（消息不可变，
  历史页天然缓存命中）；分页参数 `from_chars`，越界走 DB 回源并回填环。
- **Sidebar**：纯 `RoomSnapshot` 单 Fragment 输出；presence 变更走与消息写
  相同的快照替换路径。这是五条路线里最薄的一条，应当最先逼近传输层地板。
- **Search**：FTS5 prepared 查询 + (room, term) LRU 结果缓存；term 规范化
  与 Rails/Rust 的 literal 语义一致。缓存未命中时单查询预算 ≤ 50 µs
  （页缓存命中假设，未验证，M5 实测）。
- **Post a message**：3.4 的六步。目标 10,000 req/s 下 WAL 单写者不构成
  瓶颈；瓶颈预期在 form 解析与 richtext 消毒，两者都做成
  无分配/arena 分配。

## 七、基准测试与验收

方法与基线对齐：**第一基准是 once-campfire-rust 仓库自带的 bench 压测器**
（同一工具分别打 Rust 版与 C++ 版，排除压测器差异）；本仓库另附一个
asio 写的负载器做回归用（16 keep-alive 连接、固定时长、warmup 后计数，
p50/p99 同步记录）。应用侧 `taskset` 绑 4 个硬件线程，loopback，
`TCP_NODELAY` 开启。

验收判据（M6 门禁）：第七节条件下，五条路线全部超过第一节「基线」列，
即 36,260 / 40,872 / 34,672 / 33,299 / 6,896 req/s；「设计目标」列作为
努力方向，不作为门禁。每条路线记录：req/s、p50、p99、用户态 CPU 占用、
每请求分配次数（mimalloc 统计）、每请求系统调用数（strace -c，开发期）。

补充验收（非门禁）：同一 Rails 数据库文件可被直接打开并正确渲染
（schema 兼容冒烟测试）；bot webhook 与 WebSocket 广播的功能冒烟。

## 八、里程碑

| 阶段 | 内容 | 出口判据 |
|---|---|---|
| M0 | 仓库脚手架：mcpp.toml、asio/sqlite 依赖解析、`import std` 冒烟 | `mcpp build` 通过 |
| M1 | HTTP 解析 + 路由 + 静态 stub 响应 + 负载器 | 测得传输层地板 req/s（预期 ≥ 100k，实测后回填） |
| M2 | DB 层、模型、Rails schema 兼容、cookie/会话 | 用 Rails 数据启动并返回真实用户页 |
| M3 | views + Fragment + RoomSnapshot；Room/Messages/Sidebar 三条 GET | 三条路线达到基线 |
| M4 | Post 路径 + 广播（0 订阅者）+ 快照失效 | Post 达到基线 |
| M5 | FTS5 + 搜索 + 结果缓存 | Search 达到基线 |
| M6 | 逐路线剖析与调优（-O3/LTO/-march 记录、arena、writev 形态） | 五条路线全部超过基线，出对比表 |
| M7（可选） | cable 全量、richtext 全管线、storage、TLS/前端服务器、jobs | 与 Rust 版功能面对齐 |

## 九、风险与未验证项

- `compat.sqlite3` 描述符的 FTS5 开关未验证（3.5）；备选是 5.2 的自建包。
- `chriskohlhoff.asio` 模块未导出 SSL 与 Unix 域套接字；TLS 阶段直接用
  `compat.openssl` 组合，不走 asio::ssl。
- GCC 16 模块 + Asio 头重：BMI 首编成本高，separate compilation 已由
  描述符处理；增量构建成本 M0 实测。
- HTML 与 Rails 字节级一致的完整验证依赖 Rails 参考输出，列为 M3 的
  golden test，不阻塞性能门禁。
- 会话缓存的失效正确性（用户被删/改后 60 s 窗口）是有意接受的语义偏差，
  与 review 确认。
- 基线数字来自三方 README/PR，本机复测 Rust 版作为 M6 的对照动作，
  差异过大时以本机复测为准并更新基线。

## 十、被否掉的替代方案

- **D1 线程池 + 任务队列**（go 风格）：读路径引入跨线程跳转与一次
  唤醒延迟；16 连接下 thread-per-core 无负载不均问题，跳转只有成本没有收益。
- **D2 io_uring 作为第一阶段传输**：16 并发下每请求 2–3 次系统调用，
  epoll 的系统调用开销不在瓶颈路径上；io_uring 换来 liburing 依赖与
  平台面收窄。保留为 M6 后的可选替换，`http::Server` 接口已按可替换设计。
- **D3 ORM / 查询构建器**：读路径预算里 DB 查询主要发生在缓存回源，
  手写 prepared 语句 + 语句缓存已经是最小形态，多一层抽象只有开销。
- **D4 Boost.Beast**：HTTP 解析与连接管理自写约千行量级，换来零拷贝
  字段视图与编译期路由；Beast 的流抽象在 keep-alive 热路径上多一层间接。
  Asio 仅作为事件循环使用（Beast 依赖 asio 的那部分面被绕开）。
- **D5 每请求 std::string 拼接响应**：与 iovec 方案对比，多一次整页
  memcpy 与多次分配；直接被 3.2 的组装路径替代。
- **D6 自建存储引擎替代 SQLite**：放弃 Rails/Rust 数据兼容这一既定目标，
  兼容性是本仓库的立身之本，性能瓶颈也不在 SQLite 读路径。
