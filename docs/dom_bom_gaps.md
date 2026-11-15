# Browser API presence inventory

Last reviewed: `2026-11-15` · Source: `7be41ab5b107aff2aa411b7b139b350e6b70f43d`

本轮按用户要求改为 **先覆盖 API，再完善浏览器一致性**。以下是实际浏览器与当前 SDK 的存在性差分，不是沿用历史缺口表。

此表保留 **BIND-01 共享 SDK 基线**，便于追溯。后续第一批 PRES-01 已在本地候选中实现；新增名称、剩余缺口和本地验收见 [presence_01_report.md](presence_01_report.md) / [presence_01_api_delta.json](presence_01_api_delta.json)。共享 SDK 仍是上述基线。

## 1. 统计口径

- 参考：macOS arm64，Google Chrome **157.0.8103.51**，新建隔离 headless profile，本地 HTTP loopback secure context，关闭后台网络且阻断非 loopback DNS。
- SDK：实际共享 `iv8-test`，包版本 0.0.1，V8 15.0.245.19，native SHA-256 `d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`。
- 同一 probe 枚举 22 个固定对象的自身和原型链，按字符串成员名去重；不计数字索引和 `constructor` 元数据，不把 Object.prototype 通用成员加进参考分母。
- 判定“有没有”时用实际 `key in object`，因此继承到的同名方法也算已暴露。`location.valueOf` 就属于这种情况；其定义位置是否像 Chrome 留到后续。
- Window 表剔除 ECMAScript 全局内建；单列接口/构造器入口、事件属性和普通属性/方法。带 own prototype 的函数归接口入口，包含禁止公开 new 的接口和别名，并不表示它可直接构造。
- **已暴露只表示名称存在**；固定值、no-op、明确拒绝、部分实现另列。未列入源码语义审计的成员不自动认证为完整实现。`document.all` 按 descriptor/存在性判定，不用其特殊的 typeof/真假值。
- DOM 对象继承大量共同 API，表格不能相加当作独立 API 数量或全浏览器覆盖率。参考是本次 Chrome 配置的曝光面，不包含所有隐藏、实验或其他浏览器特有接口。
- 本轮未递归展开全部全局接口的原型/静态方法，以及样式、图形、媒体、设备子接口。例如 URL 静态方法不是此表的独立统计目标。

原始快照、统一 probe、二次 membership 核验、哈希及 CSV 位于忽略目录 `data/browser-api-inventory-2026-11-15/`。可查询的完整结果见 [browser_api_inventory.json](browser_api_inventory.json)。

## 2. 主对象数量

| 对象 / 范围 | Chrome 成员 | iv8 已暴露 | 完全缺失 |
|---|---:|---:|---:|
| Window 普通属性/方法 | 128 | 79 | 49 |
| Window `on*` 事件属性 | 125 | 18 | 107 |
| Window 接口/构造器全局入口 | 927 | 98 | 829 |
| `location` | 15 | 14 | 1 |
| `document` | 301 | 86 | 215 |
| `navigator` | 70 | 26 | 44 |
| `screen` | 15 | 12 | 3 |
| `history` | 8 | 8 | 0 |
| `performance` | 22 | 9 | 13 |
| `crypto` | 3 | 2 | 1 |
| `console` | 25 | 14 | 11 |
| `localStorage` | 6 | 6 | 0 |
| `sessionStorage` | 6 | 6 | 0 |

Window 三组共 **1180 个浏览器相关名称：195 已暴露、985 缺失**。原始 globalThis 数为 Chrome 1246、Page 261、裸 JSContext 67；过滤口径不同，不要混用这些数字。

## 3. 嵌套对象和 DOM 样本

| 对象 | Chrome 成员 | iv8 已暴露 | 完全缺失 |
|---|---:|---:|---:|
| `visualViewport` | 14 | 12 | 2 |
| `screen.orientation` | 9 | 2 | 7 |
| `navigator.connection` | 9 | 4 | 5 |
| `navigator.permissions` | 1 | 1 | 0 |
| `navigator.storage` | 4 | 3 | 1 |
| `scheduler` | 2 | 1 | 1 |
| `element`（`div` 样本） | 369 | 81 | 288 |
| `form` | 386 | 93 | 293 |
| `input` | 428 | 98 | 330 |
| `canvas` | 384 | 84 | 300 |
| `event` | 22 | 10 | 12 |

`document` 的 215 个缺失名包含 **117 个 `on*`** 和 98 个其他成员；`div` 的 288 个缺失名包含 **110 个 `on*`**。Form/Input/Canvas 还重复继承这些共同缺口。全局缺失接口中，SVG 前缀有 97 个、CSS 61 个、GPU 35 个、XR 50 个，适合按接口族组织工作。

## 4. 主要完全缺失 API

### Window 普通属性/方法（全部 49 项）

`CSS`, `GPUBufferUsage`, `GPUColorWrite`, `GPUMapMode`, `GPUShaderStage`, `GPUTextureUsage`, `NodeFilter`, `PERSISTENT`, `TEMPORARY`, `caches`, `captureEvents`, `chrome`, `close`, `cookieStore`, `crashReport`, `createImageBitmap`, `credentialless`, `customElements`, `documentPictureInPicture`, `event`, `external`, `fence`, `fetchLater`, `find`, `getComputedStyle`, `getScreenDetails`, `getSelection`, `indexedDB`, `launchQueue`, `navigation`, `offscreenBuffering`, `open`, `originAgentCluster`, `queryLocalFonts`, `releaseEvents`, `reportError`, `requestResize`, `showDirectoryPicker`, `showOpenFilePicker`, `showSaveFilePicker`, `speechSynthesis`, `styleMedia`, `trustedTypes`, `viewport`, `webkitCancelAnimationFrame`, `webkitRequestAnimationFrame`, `webkitRequestFileSystem`, `webkitResolveLocalFileSystemURL`, `when`

### 常用缺失接口/构造器入口

`DOMException`, `AbortController`, `AbortSignal`, `Headers`, `Request`, `Response`, `FormData`, `DOMParser`, `XMLSerializer`, `Attr`, `NamedNodeMap`, `NodeList`, `HTMLCollection`, `HTMLFormControlsCollection`, `HTMLOptionsCollection`, `FileList`, `Range`, `Selection`, `TreeWalker`, `NodeIterator`, `UIEvent`, `InputEvent`, `PointerEvent`, `WheelEvent`, `ProgressEvent`, `ErrorEvent`, `PromiseRejectionEvent`, `HashChangeEvent`, `PopStateEvent`, `SubmitEvent`, `MessageChannel`, `MessagePort`, `BroadcastChannel`, `ResizeObserver`, `IntersectionObserver`, `PerformanceObserver`, `DOMRect`, `DOMRectReadOnly`, `DOMPoint`, `DOMMatrix`, `WebSocket`, `EventSource`, `SharedWorker`, `ServiceWorker`

完整 829 个缺失接口入口保存在 JSON 的 `objects.window.categories.interface_entries.missing`，未将其逐个列为开发里程碑。

### Navigator（全部 44 项）

`appVersion`, `bluetooth`, `canShare`, `clearAppBadge`, `clipboard`, `cpuPerformance`, `credentials`, `deviceMemory`, `devicePosture`, `geolocation`, `getGamepads`, `getInstalledRelatedApps`, `getUserMedia`, `gpu`, `hid`, `ink`, `keyboard`, `locks`, `login`, `managed`, `mediaCapabilities`, `mediaDevices`, `mediaSession`, `presentation`, `registerProtocolHandler`, `requestMIDIAccess`, `requestMediaKeySystemAccess`, `scheduling`, `serial`, `serviceWorker`, `setAppBadge`, `share`, `storageBuckets`, `unregisterProtocolHandler`, `usb`, `userActivation`, `userAgentData`, `vibrate`, `virtualKeyboard`, `wakeLock`, `webkitGetUserMedia`, `webkitTemporaryStorage`, `windowControlsOverlay`, `xr`

### Document 常用入口与逻辑能力

`write`, `writeln`, `open`, `close`, `createEvent`, `createAttribute`, `createAttributeNS`, `createElementNS`, `createRange`, `createTreeWalker`, `createNodeIterator`, `importNode`, `adoptNode`, `getRootNode`, `normalize`, `compareDocumentPosition`, `implementation`, `doctype`, `scrollingElement`, `charset`, `inputEncoding`, `lastModified`, `getSelection`, `elementFromPoint`, `elementsFromPoint`, `styleSheets`, `fonts`, `adoptedStyleSheets`, `onclick`, `oninput`, `onchange`, `onkeydown`, `onkeyup`, `onsubmit`

### Location / 其他小对象

- `location`：`ancestorOrigins`。
- `screen`：`isExtended`, `onchange`, `when`。
- `performance`：`addEventListener`, `clearResourceTimings`, `dispatchEvent`, `eventCounts`, `interactionCount`, `memory`, `navigation`, `onresourcetimingbufferfull`, `removeEventListener`, `setResourceTimingBufferSize`, `timing`, `toJSON`, `when`。
- `crypto`：`subtle`。
- `console`：`clear`, `context`, `countReset`, `createTask`, `dirxml`, `groupCollapsed`, `memory`, `profile`, `profileEnd`, `timeLog`, `timeStamp`。
- `visualViewport`：`onscrollend`, `when`。
- `screen.orientation`：`addEventListener`, `dispatchEvent`, `lock`, `onchange`, `removeEventListener`, `unlock`, `when`。
- `navigator.connection`：`addEventListener`, `dispatchEvent`, `onchange`, `removeEventListener`, `when`。
- `navigator.storage`：`getDirectory`。
- `scheduler`：`yield`。
- `event`：`AT_TARGET`, `BUBBLING_PHASE`, `CAPTURING_PHASE`, `NONE`, `cancelBubble`, `composed`, `composedPath`, `initEvent`, `isTrusted`, `returnValue`, `srcElement`, `timeStamp`。

### Element / Form / Input / Canvas

- 通用缺口：`dataset`、`attributes`、`getAttributeNode*`、命名空间属性方法、`getRootNode`、`normalize`、`compareDocumentPosition`、`focus`/`blur`/`click`、事件属性、`attachShadow`、几何/滚动/布局及 ARIA 反射等。
- Form 相对 div 样本额外缺：`acceptCharset`、`autocomplete`、`length`、`name`、`rel`、`relList`。
- Input 常用额外缺：`files`、`labels`、`list`、`min`/`max`/`step`、`minLength`/`maxLength`、`pattern`、`multiple`、`formAction`/`formMethod` 等覆盖属性、`selectionStart`/`selectionEnd`、`setSelectionRange`、`setRangeText`、`select`、`valueAsDate`/`valueAsNumber`、`stepUp`/`stepDown`、`showPicker`。
- Canvas 常用额外缺：`toDataURL`、`toBlob`、`captureStream`、`transferControlToOffscreen`。其 `getContext` 名称已存在，但没有绘图后端。

## 5. 存在但不应当算“功能已补齐”

| 已暴露入口 | 当前能力 |
|---|---|
| `fetch` | Promise 明确拒绝，尚无请求执行 |
| `XMLHttpRequest` / `Worker` | 构造器存在，执行构造即 NotSupportedError |
| `navigator.sendBeacon` | 固定 false，无请求发送 |
| `canvas.getContext` | 固定 null，无 Canvas/WebGL 后端 |
| Location 写入及 assign/replace/reload | 记录离线导航意图，不执行导航 |
| History back/forward/go | 记录有效遍历/重载意图，不执行历史遍历 |
| Window 滚动、焦点、移动/缩放/UI 方法 | 多数 no-op 或固定返回，无布局/UI联动 |
| Form submit/requestSubmit | 可调用的 no-op，无真实提交 |
| navigator.storage / permissions | 固定估算或权限状态，无实际存储持久化/设备权限 |
| localStorage/sessionStorage | 方法名齐，提供 per-Page 内存存储，非完整 origin/持久化/跨页事件模型 |

源码审计另有 183 个精确成员的状态和行号：4 个硬不可用、16 个 intent-only、22 个 no-op、85 个固定/配置值、55 个有限实现及一个明确 native 实现示例。这是选择性审计，不能把这些数量和“完全缺失”相加。固定值也不自动代表错误。JSON 中的 `source_review` 给出对应证据。

## 6. 单列的引擎缺口

`Intl` 和 `Temporal` 在 Chrome 中存在，但当前裸 JSContext 和 Page 都缺失。macOS/manylinux V8 构建脚本明确设置 `v8_enable_i18n_support=false` 和 `v8_enable_temporal_support=false`，当前 BUILD_INFO 也记录了关闭状态。这两个名字从 Window 浏览器 API 数量中剔除并单独记录；其中 Intl 影响常见语言、数字、日期和时区调用，值得单独优先评估。

## 7. 建议的覆盖顺序

| 批次 | 优先补齐的 API 族 | 基础交付目标 |
|---|---|---|
| 1. BOM / 事件基础 | appVersion、deviceMemory、userAgentData、userActivation、ancestorOrigins；常用 on*；Event 缺字段/方法；Console 缺方法 | 常用脚本可读取或调用，并有基本用例 |
| 2. DOM 逻辑与反射 | dataset、Attr/NamedNodeMap、命名空间基础、getRootNode/normalize/compareDocumentPosition、importNode/adoptNode、Document 创建/DOMParser | 复用现有树/解析器，无需布局或网络 |
| 3. Web 数据对象 | DOMException、AbortController/Signal、Headers/Request/Response、FormData、Range/遍历器、更多事件类 | 离线对象和基本处理链可执行 |
| 4. 执行子系统 | 可执行 fetch/XHR、Worker/MessageChannel、IndexedDB/Cache 等 | 分别约定离线资源、调度、origin 和安全边界 |
| 5. 渲染与设备 | computed style/布局/几何、Canvas/WebGL、Audio/GPU/媒体、真实窗口/设备权限 | 独立系统设计及样本，不按构造器名字逐项铺开 |

按 API 族合批开发；第一阶段验收基础存在性和可执行用例，同时建立可扩展的共享原型链，后续再集中提升描述符、函数形态和细节一致性。已有 History、命名访问、数据转换和生命周期回归保留。盘点本身没有修改运行时；PRES-01 的实现和本地构建是后续单独记录的批次，尚未发布。

## 8. 证据与规范分组

- 机器可读全名单：[browser_api_inventory.json](browser_api_inventory.json)。每对象含所有缺失名、已暴露名、分类计数和已审计限制。
- 本地原始 Chrome/Page/裸 JSContext 快照、统一 probe、membership 核验及 `missing-members.csv`：`data/browser-api-inventory-2026-11-15/`。CSV 是 qualified object/member 路径，继承重复成员不可合计为独立 API。
- 规范分组参考：[HTML Window/Location](https://html.spec.whatwg.org/multipage/nav-history-apis.html#the-window-object)、[HTML Navigator](https://html.spec.whatwg.org/multipage/system-state.html#the-navigator-object)、[DOM Document](https://dom.spec.whatwg.org/#interface-document)。数量来自本轮实测，不是规范页面推算。
