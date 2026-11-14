# page_state.cpp 拆分方案

状态：**阶段 0、阶段 1 验收完成，下一步阶段 2**（方案确认于 `2026-11-08`）。基线为 `src/core/page_state.cpp`，13,058 行、570 KB。

这是一项内部维护工作，不是 M11-53。它不改变任何 Python 或 JS 可见行为，也不改变绑定机制。

## 0. 目标与边界

**目标**

- 把 `page_state.cpp` 拆成按职责划分的模块。除 `install_page` 拆分后的安装器外，**单个文件不超过约 1,200 行**。
- `page_state.cpp` 最终只保留 `PageState` 的生命周期和对外门面，目标 **约 400 行**。
- 让 DOM 数据层（解析、序列化、选择器、表单校验）**不依赖 V8**，可以单独做差分测试。
- 让监控层（DevTools、Reflect/Object 钩子、访问追踪查询包装）在物理上和 Page 运行时分开，符合 CLAUDE.md 的分层规则。
- 让后续工作（统一绑定机制、M11-53 等）都能落在一两个文件里完成。

**不做的事（明确排除）**

- 不改任何 JS 可见的形状：属性名、描述符、`toString`、原型链、追踪事件都保持不变。
- 不把 JS bootstrap 改成原生实现，也不把拦截器式 HostObject 迁到 `web_interface`。这些是拆分完成后的下一阶段。
- 不改 `include/iv8/page_state.h` 的公开接口，也不改 `page_binding.cpp` 和 Python 层。
- 不顺手修 bug。拆分中发现的问题记入 §7 的问题清单，单独处理。

## 1. 现状结构（按行号）

整个文件除开头的三个 `iv8::` 结构体外，第 72–11164 行都位于**一个匿名命名空间**里，之后是 `PageState` 成员函数的实现。

| 行号范围 | 约行数 | 内容 | 目标模块 |
|---|---|---|---|
| 55–76 | 20 | `PerformanceEntryRecord`、`PerformanceStore`、`MediaQueryListStore` | `web/performance`、`bom/media_query` |
| 77–149 | 70 | 前向声明，`fill_os_random_bytes`（平台 RNG） | `internal/common`、`web/crypto` |
| 150–207 | 60 | `HostProbe` | `bom/host_probe` |
| 208–323 | 115 | `ConsoleHost` | `bom/console` |
| 324–344 | 20 | `v8_string`、`dom_exception` | `internal/common` |
| 345–504 | 160 | Web Storage（`localStorage` / `sessionStorage`） | `bom/web_storage` |
| 505–592 | 90 | `ConnectionHost`、`ScreenOrientationHost` | `bom/navigator_parts` |
| 593–861 | 270 | StorageManager、Permissions / PermissionStatus | `bom/navigator_parts` |
| 862–947 | 85 | `MediaQueryListHost`、工厂 | `bom/media_query` |
| 948–1119 | 170 | `NavigatorHost` | `bom/navigator` |
| 1120–1203 | 85 | `VisualViewportHost` | `bom/visual_viewport` |
| 1204–1297 | 95 | `LocationHost` | `bom/location` |
| 1298–1512 | 215 | `DomNode`、大小写、树遍历辅助函数 | `dom/dom_node` |
| 1513–1936 | 420 | `ScriptRecord`、HTML 解析、序列化 | `dom/html_parser`、`dom/html_serializer` |
| 1937–2303 | 370 | 查找与收集、表单规范化、控件辅助 | `dom/dom_query`、`dom/form_controls` |
| 2304–2398 | 95 | `ValidityInfo`、校验信息 | `dom/form_controls` |
| 2399–2596 | 200 | 选择器**匹配**（解析已在 `selector_parser`） | `dom/selector_match` |
| 2597–2665 | 70 | `detach_from_parent`、兄弟节点、可执行脚本判断 | `dom/dom_node` |
| 2666–2848 | 180 | MutationObserver 结构体；事件停止 / 取消辅助 | `host/mutation_observer`、`host/event_support` |
| 2849–3172 | 325 | `EventTargetHost` | `host/event_target` |
| 3173–3244 | 70 | `ScreenHost` | `bom/screen` |
| 3245–3364 | 120 | `WindowEventTarget` | `window/window_event_target` |
| 3365–3394 | 30 | `FileReaderEvents`、`FileReaderState` | `web/blob_file_reader` |
| 3395–3840 | 445 | `NodeHost`、`ElementHost`、`CharacterDataHost`、`DocumentFragmentHost`、`StandaloneEventTarget` 的声明 | `host/node_hosts.h` |
| **3841–4873** | **1,030** | **`DocumentHost` 类定义（含大量内联函数体）** | `host/document_host.h` / `.cpp` |
| 4874–5054 | 180 | 实时集合（HTMLCollection 拦截器、`document.all`） | `host/live_collection` |
| 5055–5556 | 500 | MutationObserver 实现 | `host/mutation_observer` |
| 5557–5867 | 310 | `classList`（DOMTokenList） | `host/class_list` |
| 5868–6651 | 785 | DocumentHost 的树修改（insert / replace / remove / adjacent / `call_node_method`） | `host/document_tree_mutation` |
| 6652–6863 | 210 | `CharacterDataHost`、`DocumentFragmentHost` 实现 | `host/character_data_host`、`host/document_fragment_host` |
| 6864–7588 | 725 | `ElementHost::get_property` / `set_property` | `host/element_properties` |
| 7589–7623 | 35 | `watch_record`、`access_trace_record`、追踪 token | `internal/trace_hooks` |
| 7624–8069 | 445 | `ElementHost` 的校验与 `call_method`、`NodeHost::dispatch_event` | `host/element_methods` |
| 8070–8341 | 270 | 定时器、rAF、idle、对话框、滚动 / 焦点桩 | `window/window_functions` |
| 8342–8622 | 280 | `performance` | `web/performance` |
| 8623–8902 | 280 | `structuredClone` | `web/structured_clone` |
| 8903–9100 | 200 | `scheduler` 与 BOM extras 的 **JS bootstrap 源码** | `bootstrap/bootstrap_scripts` |
| 9101–9194 | 95 | `crypto` | `web/crypto` |
| 9195–9275 | 80 | 能力缺失处理、`web_interface_factory`、离线 `fetch`、`location` 槽、`install_global_function` | `web/capabilities`、`internal/common` |
| 9276–9580 | 305 | Event / CustomEvent / MouseEvent / KeyboardEvent / FocusEvent / MessageEvent 构造器 | `web/event_constructors` |
| 9581–9723 | 145 | window `on*` 处理器、`postMessage`（含 JS bootstrap） | `window/window_event_handlers`、`window/post_message` |
| 9724–9849 | 125 | TextEncoder / TextDecoder | `web/encoding` |
| 9850–10118 | 270 | URL、URLSearchParams | `web/url` |
| 10119–10688 | 570 | Blob、File、FileReader | `web/blob_file_reader` |
| 10689–10749 | 60 | `btoa` / `atob` | `web/base64_globals` |
| 10750–10848 | 100 | `DevToolsClient`、`DevToolsChannel` | `monitor/devtools_inspector` |
| 10849–11163 | 315 | Reflect / Object / hasOwn 查询包装（监控） | `monitor/reflect_hooks` |
| 11169–11697 | 530 | `DevToolsInspector` 与 `PageState` 的 devtools / watch / trace 方法 | `monitor/*`、`page_state_monitoring.cpp` |
| 11698–11955 | 260 | `window.name` / `status` / 别名 / 元数据访问器 | `window/window_metadata` |
| 11956–11993 | 40 | `create_document`、`create_web_interface` | `page_state.cpp` |
| **11994–12841** | **850** | **`PageState::install_page`（单个函数）** | 拆成各模块的 `install_*()`，见阶段 5 |
| 12842–13058 | 215 | 生命周期事件、HTML 脚本、`load`、事件循环转发 | `page_state.cpp` |

最突出的三处：

- `DocumentHost` 的定义加实现约 3,500 行。
- `ElementHost` 的实现约 1,200 行。
- `install_page` 是一个 850 行的函数。

## 2. 目标目录结构

所有新文件都是**内部实现**，放在 `src/core/browser/` 下，头文件不进入公开的 `include/iv8/`：

```text
src/core/browser/
├── internal/
│   ├── common.h / .cpp           # v8_string, dom_exception, ascii_lower/upper,
│   │                             # state_from_isolate, install_global_function
│   ├── trace_hooks.h / .cpp      # watch_record, access_trace_record, 追踪 token
│   └── install_context.h         # 阶段 5：安装器共用的上下文结构体
├── dom/                          # 不依赖 V8
│   ├── dom_node.h / .cpp         # DomNode、遍历、detach、兄弟节点
│   ├── html_parser.h / .cpp      # parse_html、属性、raw text、ScriptRecord、初始化
│   ├── html_serializer.h / .cpp
│   ├── dom_query.h / .cpp        # dfs_find、collect_*、类名 token
│   ├── selector_match.h / .cpp   # 消费 iv8/selector_parser.h 的输出
│   └── form_controls.h / .cpp    # normalize_*、控件值、ValidityInfo、校验信息
├── host/                         # DOM 宿主对象层
│   ├── event_support.h / .cpp    # 停止 / 取消 / passive 私有键
│   ├── event_target.h / .cpp     # EventTargetHost、StandaloneEventTarget
│   ├── node_hosts.h              # NodeHost / ElementHost / CharacterData / Fragment 声明
│   ├── element_properties.cpp    # ElementHost::get_property / set_property
│   ├── element_methods.cpp       # ElementHost::call_method、校验、NodeHost::dispatch_event
│   ├── character_data_host.cpp
│   ├── document_fragment_host.cpp
│   ├── document_host.h           # 只保留声明，去掉内联函数体
│   ├── document_host.cpp         # 构造、get/set/call、包装、克隆、查询
│   ├── document_tree_mutation.cpp
│   ├── live_collection.h / .cpp
│   ├── mutation_observer.h / .cpp
│   └── class_list.h / .cpp
├── bom/
│   ├── host_probe.cpp, console.cpp, navigator.cpp, navigator_parts.cpp,
│   ├── location.cpp, screen.cpp, visual_viewport.cpp, media_query.cpp,
│   └── web_storage.cpp           # 每个文件配一个很薄的 .h
├── window/
│   ├── window_event_target.h / .cpp
│   ├── window_event_handlers.cpp
│   ├── window_functions.cpp      # 定时器、rAF、idle、对话框、滚动 / 焦点桩
│   ├── window_metadata.cpp       # name / status / 别名 / 元数据
│   └── post_message.cpp
├── web/
│   ├── performance.cpp, structured_clone.cpp, crypto.cpp, encoding.cpp,
│   ├── url.cpp, blob_file_reader.cpp, base64_globals.cpp,
│   ├── event_constructors.cpp, capabilities.cpp
├── bootstrap/
│   └── bootstrap_scripts.h / .cpp  # 三段 JS 源码原样搬过来，绑定改造时再删
└── monitor/
    ├── devtools_inspector.h / .cpp
    └── reflect_hooks.h / .cpp

src/core/page_state.cpp            # PageState 生命周期与门面（约 400 行）
src/core/page_state_install.cpp    # install_page，阶段 5 后只负责编排
src/core/page_state_monitoring.cpp # PageState 的 devtools / watch / trace 成员函数
```

命名空间：匿名命名空间改为 **`iv8::browser`**。`PageState` 仍在 `iv8::` 下。

依赖方向只允许单向：

```text
internal  <-  dom  <-  host  <-  window / bom / web  <-  page_state*.cpp
                                     monitor  <-  page_state_monitoring.cpp
```

`dom/` 不能 include V8 头文件；`monitor/` 不能被 `host/`、`bom/`、`web/` 引用，它们只通过 `internal/trace_hooks.h` 记录访问。

## 3. 拆分中必须处理的技术陷阱

以下几项在单文件、匿名命名空间里不会出问题，拆开后会**静默出错**，必须在阶段 0 先处理。

1. **地址被当作身份的 token。** 访问追踪用变量地址区分对象类型：
   - `constexpr char kStorageManagerTraceToken`、`kPermissionsTraceToken`、`kJavaEnabledTraceToken`
   - `static char kPerformanceTraceToken`、`kCryptoTraceToken`、`kStorageTraceToken`

   如果把它们原样放进头文件，每个编译单元都会有一份**不同地址**的副本，追踪的身份判断会悄悄失效，测试也未必能马上发现。

   处理办法：统一改为 `inline constexpr char`（C++17 inline 变量全程序只有一个地址），或者在 `trace_hooks.cpp` 里定义、头文件里 `extern` 声明。阶段 0 加一个运行时断言测试，确认跨文件取到的地址相同。
2. **匿名命名空间的内部链接。** 拆开后这些符号变成外部链接。目前其他文件（`host_object.cpp`、`context_host.cpp`、`web_interface.cpp`、`browser_compatibility.cpp`）各自也有匿名命名空间，所以要用独立的 `browser` 命名空间避免同名冲突。只在单个文件内使用的辅助函数，继续留在该文件自己的匿名命名空间里。
3. **`DocumentHost` 与 `ElementHost` 互相依赖。** `ElementHost` 持有 `DocumentHost*`，`DocumentHost` 又创建 `ElementHost`。解决办法是 `node_hosts.h` 里前向声明 `DocumentHost`，两边的实现都放在 `.cpp` 里。`DocumentHost` 中约 1,000 行的内联函数体要移出类定义，否则头文件会把依赖重新传染给所有引用方。
4. **`ExternalPointerTypeTag` 全部是 `kExternalPointerTypeTagDefault`。** 拆分时值保持不变，`internal/common.h` 里集中声明，不要分散到各处。改成各自独立的 tag 能防止类型混淆，但那是行为变化，记入 §7，不在本次处理。
5. **平台相关代码。** Windows 下的 `SystemFunction036` 声明和 Linux 的 `<sys/random.h>` 只跟着 `web/crypto.cpp` 走，不能留在公共头文件里。
6. **V8 Inspector 头文件。** `v8-inspector.h` 必须在 `context_host.h` 之后 include（源码注释里提到的 COMPILER 宏问题），而且只允许出现在 `monitor/devtools_inspector.cpp` 里。
7. **编译时间。** 拆成约 45 个编译单元后，每个都要 include V8 头文件。建议给 `_core` 加上 `target_precompile_headers`，预编译 `v8_headers.h` 和 `<pybind11/pybind11.h>`，否则全量编译可能比现在更慢。增量编译会明显变快。

## 4. 分阶段步骤

按 `2026-11-14` 用户确认，阶段一剩余内容与阶段二分别汇总为阶段 PR。每个阶段先完成语法检查、搬运审计与测试，再提交 PR 并执行 V8 链接构建和平台验收。阶段二完成后等待用户确认。

### 阶段 0：准备（2–3 个 PR）

- **0.1 本地真 V8 构建循环。** 当前本地只能编译不含 V8 的骨架版本，大部分测试会被跳过。有两种方案：
  - 用 `tools/build_v8_macos.sh` 在本机编一次 V8 monolith（耗时长，但只需一次）；
  - 从 CI 缓存的 monolith 产物下载使用。

  然后在 `docs/dev_setup.md` 记录"本地编译 + 跑完整 pytest"的命令。**没有这一步，后面每一步都只能靠 CI 验证，节奏会非常慢。**
- **0.2 原地改造，不移动代码。**
  - 把匿名命名空间改成 `iv8::browser`（本文件专用的辅助函数可以保留 static）；
  - 按 §3 第 1 条修正 token，并加地址一致性测试；
  - 新建 `src/core/browser/` 目录和 `internal/common.h`，先把 `v8_string`、`dom_exception`、`state_from_isolate` 等通用辅助函数移过去；
  - 给 CMake 加上新的源文件列表和预编译头。
- **0.3 搬运审计脚本 `tools/audit_move.py`。** 先用 libclang 或简单的花括号匹配，提取旧文件里每个顶层定义的规范化 token 流；再提取新文件集合的同一组定义，逐个比对。纯搬运的 PR 必须输出"0 处差异"，结果贴在 PR 描述里。这样审查者不用逐行读几千行的 diff。

### 阶段 1：叶子模块（已完成；1.1 独立 PR，1.2–1.8 汇总 PR）

这些模块只依赖 `internal/` 和 `ContextState`，与 DOM 无关，可以并行处理：

1. `web/crypto`（含平台 RNG）、`web/base64_globals`、`web/encoding`
2. `web/url`、`web/structured_clone`
3. `web/performance`（连同 `PerformanceStore`）
4. `bom/console`、`bom/host_probe`、`bom/web_storage`
5. `bom/navigator_parts`（Connection、Orientation、StorageManager、Permissions）、`bom/media_query`
6. `bom/navigator`、`bom/visual_viewport`、`bom/location`
7. `web/event_constructors`、`web/capabilities`
8. `bootstrap/bootstrap_scripts`（三段 JS 原样搬运）

完成后预计 `page_state.cpp` 减少约 2,800 行。

### 阶段 2：DOM 数据层（按确认汇总为一个阶段 PR，不依赖 V8）

1. `dom/dom_node`（含 detach / siblings）
2. `dom/html_parser`、`dom/html_serializer`
3. `dom/dom_query`、`dom/selector_match`
4. `dom/form_controls`（规范化、控件值、校验信息）

这一层不依赖 V8，所以可以沿用之前 selector、cookie、base64 等提取时的做法：用 UBSan 对新旧代码跑上万条输入的差分比对。解析器和序列化器尤其值得做一次。完成后预计再减少约 1,500 行。

### 阶段 3：宿主对象层（约 7 个 PR，核心也是风险最高的部分）

按依赖顺序进行：

1. `host/event_support`、`host/event_target`（`EventTargetHost`、`StandaloneEventTarget`）
2. `host/node_hosts.h`：只移动声明，实现暂时留在原处
3. `host/document_host.h`：把内联函数体移出类定义，放到 `document_host.cpp`；声明移到头文件
4. `host/live_collection`、`host/class_list`、`host/mutation_observer`
5. `host/document_tree_mutation`
6. `host/element_properties`、`host/element_methods`、`host/character_data_host`、`host/document_fragment_host`
7. `bom/screen`、`window/window_event_target`、`window/window_event_handlers`、`window/post_message`、`window/window_functions`、`window/window_metadata`、`web/blob_file_reader`（依赖 `DocumentHost`，所以放在这里）

第 3 步是整个方案里唯一一个**大批量移动内联函数体**的 PR，审计脚本必须覆盖到它。

### 阶段 4：分离监控层（约 2 个 PR）

1. `monitor/devtools_inspector`（`DevToolsClient`、`DevToolsChannel`、`DevToolsInspector`）
2. `monitor/reflect_hooks`，以及把 `PageState` 的 devtools / watch / trace 成员函数移到 `page_state_monitoring.cpp`

完成后 Page 运行时的代码不再 include `v8-inspector.h`。

### 阶段 5：拆分 `install_page`（约 3 个 PR，唯一的非纯搬运阶段）

`install_page` 是一个 850 行的函数，它按顺序把各类对象安装到全局对象上。做法如下：

- 在 `internal/install_context.h` 定义 `InstallContext`，包含 `isolate`、`context`、`global`、`PageState&`、`environment` 和 host 对象列表。
- 每个模块提供自己的 `install_xxx(InstallContext&)`，例如 `install_timers`、`install_dialogs`、`install_window_geometry`、`install_performance`、`install_crypto`、`install_bar_props`、`install_bootstrap_scripts`。
- `install_page` 只按**原来的顺序**依次调用这些函数。安装顺序会影响属性枚举顺序，而 `Object.keys(window)` 的结果是可以被检测的，所以顺序必须完全一致。

验证：在阶段 5 开始前，先补一个**全局对象快照测试**。用 `Reflect.ownKeys` 遍历 `window`、`document`、`navigator` 等对象及其原型链，记录每个属性的名字、顺序、描述符类型、`typeof`、`Function.prototype.toString` 和 `length`，生成一份 JSON 金标准。阶段 5 的每个 PR 都必须和这份快照完全一致。这个测试以后也可以直接作为"统一绑定机制"阶段的基础。

完成后 `page_state.cpp` 约 400 行，`page_state_install.cpp` 约 150 行。

## 5. 验证策略

| 层级 | 每个 PR | 每个阶段结束 |
|---|---|---|
| 搬运审计（`audit_move.py`） | 纯搬运 PR 必须 0 差异 | — |
| 编译 | `-Wall -Wextra -Werror`，覆盖 macOS clang / Linux gcc / MSVC | 同左 |
| 本地完整 pytest（真 V8） | 必须通过 | 必须通过 |
| V8 CI（Linux / Py3.13） | 必须通过（已是合并门禁） | 必须通过 |
| 三平台 release 矩阵（Py3.11–3.14） | 不要求 | **必须通过** |
| 全局对象快照 | 阶段 5 每个 PR 都要求 | 必须一致 |
| Chrome 探针（74 项）与原始 capture（75/75） | — | 必须一致 |
| DOM 差分测试 | 阶段 2 每个 PR | — |

与现有流程相比，这里有一处调整：**三平台 release 矩阵只在每个阶段结束时跑一次**，不再每个 PR 都跑。理由是纯搬运 PR 的风险在编译阶段就能暴露，而 MSVC 的编译检查可以单独作为 PR 检查项。这样整个拆分要跑约 5 次完整矩阵，而不是约 25 次。

## 6. 文档与进度记录

- 只新建一份 `docs/page_state_split.md`，包含本方案和一张进度表：PR、源码 commit、CI run ID、`page_state.cpp` 剩余行数、审计结果。
- 每个阶段结束时，在 `current_status.md` 和 `next_tasks.md` 各更新**一行**，不必每个 PR 都同步更新 8 份交接文档。
- 每个阶段结束时更新 `architecture.md` 的 §13 布局说明。

## 7. 拆分中发现的问题（不在本次修复，单独排期）

从目前阅读代码已经看到的：

- 阶段 1.1 的独立编译检查发现两类既有告警。`btoa` 的有符号循环索引与 V8 无符号长度比较产生 `-Wsign-compare`；`watch_registry.h` 中 `WatchHit` 聚合初始化省略后续字段，产生 `-Wmissing-field-initializers`。搬迁保留原定义，独立检查分别使用 `-Wno-error=sign-compare`、`-Wno-error=missing-field-initializers` 记录告警，并抑制 V8 头文件的未使用参数告警；这些项目后续另行清理。
- 三段 JS bootstrap（`scheduler.postTask`、BOM extras 中的 `matchMedia` 等、`postMessage`）的 `toString` 与原生函数不一致，违反 CLAUDE.md 的原生绑定规则。拆分后它们集中在 `bootstrap/`，下一阶段逐个改成原生实现。
- `scheduler` 是 `Object.create(null)` 创建的，没有 `Scheduler` 接口和原型链。
- 所有 `ExternalPointerTypeTag` 都是默认值，无法在内部字段层面防止类型混淆。
- 拦截器式 HostObject（navigator、location、screen 等）与 `web_interface` 原生模板两套机制并存，统一工作留到下一阶段。

## 8. 工作量与顺序总结

| 阶段 | PR 数 | `page_state.cpp` 剩余 | 风险 |
|---|---|---|---|
| 0 准备 | 2–3 | ~13,000 | 低（token 修正需要测试保护） |
| 1 叶子模块 | ~8 | ~10,200 | 低 |
| 2 DOM 数据层 | ~4 | ~8,700 | 低到中（有差分测试兜底） |
| 3 宿主对象层 | ~7 | ~2,100 | 中到高 |
| 4 监控层 | ~2 | ~1,150 | 低 |
| 5 安装器 | ~3 | ~400 | 中（有快照测试兜底） |

共约 26 个 PR。阶段 1 和阶段 2 可以交错或并行；阶段 3 依赖阶段 2；阶段 5 必须最后做。

**建议先确认的三个决策：**

1. 本地 V8 构建循环采用哪种方式：本机编译，还是用 CI 缓存的产物？
2. 是否接受"release 矩阵每阶段跑一次"的流程调整？
3. 内部命名空间 `iv8::browser` 和目录 `src/core/browser/` 的命名是否合适？
