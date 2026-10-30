# 宿主对象访问追踪执行方案

日期：2026-10-30

状态：**步骤 1–4 已实现并在真实 V8 Linux + Windows 上验证（`2026-10-30`，commit `8995b89`）；步骤 5–7 尚未实现。** 基于 `ee68790`（M11-41..45）开始。本文不分配新的已批准里程碑编号，也不改变现有 API 合同。当前边界和验证记录见 `docs/host_access_trace.md`。

## 1. 目标与执行顺序

目标：执行一段 JS 后，能说明它实际访问了哪些受监控宿主对象、哪些属性或方法，以及它探测了哪些当前环境中不存在的属性。

严格按以下顺序交付：

1. 冻结覆盖范围和日志语义。
2. 建立日志通道与对象身份。
3. **记录经过原生入口的宿主属性访问和方法调用，形成首个可用版本。**
4. **紧接着实现未实现／不存在属性的探测，不先扩展大量其他 BOM 对象。**
5. 接入其他运行时对象和返回对象。
6. 补齐方法属性读取、反射、存在性检查等操作。
7. 提供面向实际脚本的分析报告；普通 JS 对象追踪另行立项。

每步单独提交、单独验收。运行时变更前先完成对应边界文档；现有 `watch_apis`、Reflect 监控通道和 M1 `JSContext` 合同保持原有语义。

## 2. “经过原生入口”准确指什么

当前 `src/core/host_object.cpp` 有三个入口：

- `property_getter`：读取由 `HostObject::property_names()` 注册的属性。
- `property_setter`：写入由 `writable_property_names()` 注册的可写属性。
- `method_callback`：调用由 `method_names()` 注册的方法，并成功识别宿主接收者。

在这里插入记录逻辑，只能证明这些入口实际被执行。它不是 V8 中任意属性访问的总入口。

| JS 行为 | 首个可用版本承诺 | 原因／边界 |
|---|---|---|
| `navigator.userAgent` / `navigator['userAgent']` | 记录 `get` | 已注册原生属性 |
| `element.textContent = 'abc'` | 记录 `set` | 已注册可写属性 |
| `document.querySelector('#app')` | 记录 `call` | 进入原生方法回调 |
| `const fn = document.querySelector` | 不承诺记录 `get` | 方法作为独立函数属性安装，未进入属性 getter |
| `navigator.someMissingProperty` | 首版不记录；下一步必须补 | 没有相应原生 getter |
| 给只读属性赋值 | 不承诺记录写入尝试 | 未安装 setter；严格模式仍须按原行为抛错 |
| `window.innerWidth` | 不承诺覆盖 | 当前并非上述统一 HostObject 属性路径 |
| `'x' in navigator`、删除、枚举、属性描述符查询 | 不承诺完整覆盖 | 需要对应操作的专门入口 |
| `const obj = {x: 1}; obj.x` | 不覆盖 | 脚本创建的普通对象 |

解构、展开等语法有时会执行受监控 getter，因而产生记录；不能据此宣称完整支持这些语法涉及的枚举、原型或其他操作。

日志描述的是“进入原生处理入口”，不等价于操作成功。例如 `set` 可能被对象实现忽略，`call` 可能随后抛错。首版统一使用 `phase: 'enter'`；若以后需要返回值或成功状态，再明确设计完成／异常记录。

## 3. 第一步：覆盖清单与合同

### 要达到的效果

开发和使用者能够准确判断：哪些对象的哪些操作可见，日志为空时是否可能存在监控盲区。

### 实施工作

- 列出当前所有 HostObject 派生类及其安装路径，核对全局对象、元素、附属对象的覆盖情况。
- 将 `window`、`performance`、`crypto`、Storage、`navigator` 子对象等逐项标记为“统一入口”“独立原生回调”“JS 创建的数据对象”等实现类别，不能只按名称推断。
- 明确 `get/set/call`、对象标识、脚本来源、容量限制和读取清空行为。
- 监控开关属于 `Page`；默认关闭。过滤条件首次可只支持对象类别，避免必须预先知道属性名才能发现访问。
- 将完整覆盖表随报告返回或提供查询入口，标明当前阶段未覆盖的操作。

### 注意事项

- “监控全部”只能表示全部已接入入口，不表示所有 JS 操作。
- Python 构造参数、方法名等在边界文档确定后实现；下文名称是建议，不是已有 API。
- 本方案不扩展 M1 `JSContext`。

### 验收

对第 2 节每一条样例，都能给出明确的“会记录／不记录／后续阶段处理”结论。

## 4. 第二步：日志通道和对象身份

### 要达到的效果

不同 DOM 节点可区分，同一底层节点的多个包装对象可关联；日志具有稳定顺序、有限容量和明确生命周期。

### 实施工作

建议提供以下 Page API（待实现）：

```python
page.start_access_trace(max_events=10000)
report = page.read_access_trace(clear=True)
page.stop_access_trace()
```

建议语义：start 开启新会话并清空旧记录；stop 停止新增记录但保留已有记录；read 返回事件和覆盖信息。再次 load 保留监控配置，递增 generation，旧记录携带旧 generation；dispose 后拒绝读取依赖页面状态的接口，与现有生命周期规则一致。

事件最低字段：

```text
seq, generation, object_id, object_type, member,
operation, phase, resource_name, source_kind
```

- `object_id` 在页面代次内绑定宿主实体；DOM 绑定底层节点，而不是临时 JS 包装对象。
- ID 使用内部生成的编号，不输出原生地址。
- `object_type` 与可选展示标签用于阅读；不声称恢复源码变量名或完整表达式路径。
- 派生接收者与属性所属对象不同的场景，应说明 object_id 表示宿主 backing；后续需要时另加 receiver_id。
- 报告包含容量、丢弃数、覆盖范围；满后采用明确策略，例如保留最早 N 条并累计 dropped_events。
- C++ 回调期间只写原生缓冲；Python 调用 read 时才构造 Python 列表和字典。

### 注意事项

- 热路径先判断开关，关闭时不格式化路径、不捕获堆栈；不宣称绝对零开销。
- 不保留不必要的 V8 强引用，不让日志延长 DOM／上下文寿命。
- 不在日志锁内执行 V8 操作、Python 回调或 Inspector 暂停。
- 日志失败不能变成脚本的新异常；计数／截断策略需覆盖字符串、容量和分配失败路径。
- 脚本名必须来源可靠；异步任务不能盲用上次 eval 的名称，拿不到时记空值或 unknown。
- 原生入口也可能被运行时初始化、DevTools 求值或结果转换间接调用。首版可限定于受控执行区间，区分执行来源；无法确认来源的记录标为 unknown，不虚称都是目标脚本访问。

### 验收

同一节点两次查询产生相同底层 ID；不同节点 ID 不同；重载后代次改变；多个 Page 日志互不混用；容量达到上限后报告明确显示丢弃数；停止、读取、重启行为符合约定。

## 5. 第三步：首个可用版本——原生 get / set / call

### 要达到的效果

无需改写目标 JS，即可看到已接入原生属性的普通点访问／方括号访问，以及原生方法调用。

### 实施工作

- 在 `property_getter`、`property_setter`、`method_callback` 中识别宿主后、调用其具体实现前记录事件。
- 首版对象范围以盘点确认的 `navigator/screen/location/document/element/console` 等统一 HostObject 路径为准。
- 新通道记录所有已接入操作；旧 `watch_apis` 继续服务旧 API，不能让旧通道在原有调用点和新通用入口各记一次。
- 先记录操作元数据。参数、返回值、堆栈作为后续可选扩展；基本类型若采集也须限制长度。
- 所有原有返回值、异常、属性描述符、只读规则、this 处理和对象身份保持现状。

### 注意事项

- 不重定义 JS 属性，不移除 `ReadOnly` / `DontDelete` 来方便监控。
- 不对复杂对象执行 `JSON.stringify`、`toString` 或递归取属性来打印日志。
- 不读取元素的 JS `id` 属性来构建标签；必要时从底层元数据取值，避免制造新访问。
- `Reflect.get` 访问原生属性时，旧反射通道和新属性通道可以各有一条记录；这是两个不同层面的证据，报告应注明，不能在单一通道重复计数。
- setter 只记录真正进入的写入，不将被 V8 拒绝的只读写入伪装成“成功写入”。

### 验收

样例：

```javascript
navigator.userAgent;
navigator['language'];
const el = document.querySelector('#app');
el.textContent = 'ready';
el.getAttribute('id');
```

应出现相应的 get、get、call、set、call 事件。还需检查：

- 方法仅取引用不谎报 call；未实现属性读取不谎报已覆盖。
- 开关前后的返回值、异常和描述符一致。
- 同步嵌套调用保持事件顺序；日志格式化不会引发额外 getter。
- 定时器、Promise、事件监听器中的访问在实际执行后可见，未执行的回调没有访问日志。
- `load` 重建后监控重新生效；原生析构和 dispose 安全。

交付：可用 API、边界文档、示例、聚焦测试、真实 V8 验证记录。

## 6. 第四步：紧接首版——未实现属性探测

### 要达到的效果

脚本读取 `navigator.someMissingProperty` 后，能知道它探测了该名字，且不会因为监控而改变返回值、属性存在性或后续行为。

这是首版之后的第一优先级；在它完成前不开展大批 BOM 对象迁移。

### 实施工作

1. 在项目固定 V8 版本上做小规模验证：研究命名属性拦截器的触发顺序、未拦截返回规则，以及对已注册原生属性和原型链的影响。
2. 先限于已接入 HostObject 的字符串键读取，形成可审计的查询记录。
3. 区分“查询发生”与“已证实不存在”。建议事件采用 `operation: 'get_probe'` 和 `resolution: 'missing' | 'present' | 'unknown'`；不能确认最终查找结果时必须使用 unknown。
4. 当已有原生 getter 接管读取时，只保留一个相应层级的 get 记录，防止拦截器和 getter 重复上报同一次访问。
5. 记录后交还正常查找流程，不创建占位值，不返回万能 Proxy，不把错误改成成功。

### 必须区分的三种情况

```javascript
navigator.someMissingProperty;  // 在完整查找后确实不存在
navigator.custom = undefined;
navigator.custom;               // 存在，只是值为 undefined
Object.setPrototypeOf(navigator, {fromPrototype: 123});
navigator.fromPrototype;        // 从原型继承，不是缺失
```

不能根据“值是 undefined”或“不在 HostObject::property_names() 里”判定 missing。脚本新增属性、原型属性、方法属性都可能存在。

### 注意事项

- 不在拦截器里直接递归 `Get` 同一对象，不用会再次触发用户 getter／Proxy trap 的额外遍历来猜测存在性。
- 普通读取、`in`、own property 查询、描述符查询是不同操作。首轮不能混作一种缺失事件。
- 数字索引、Symbol、全局变量名解析单列范围；字符串路径可用不代表这些也已覆盖。
- `navigator.foo` 只记录 foo；若结果为 undefined，后面的 `.bar` 可能直接抛错，不能凭空记录 foo.bar。
- 缺失属性可能是正常功能检测或拼写错误；报告叫“缺失访问候选”，不直接生成必须实现的浏览器 API 清单。

### 验收与进入下一步的条件

- 缺失属性、值为 undefined 的自有属性、原型属性三种样例可正确区分；若只能得到查询证据，必须清楚报告 unknown，并继续解决分类缺口。
- 开启监控前后，属性读取、`in`、`hasOwnProperty`、`Object.keys`、描述符、严格／非严格模式行为一致。
- 覆盖属性动态新增／删除／遮蔽、原型 getter、getter 抛错和监控重入场景。
- 若当前拦截机制无法同时保证正确分类和原有行为，先保留 get_probe/unknown，不上线虚假的 missing 分类；记录阻碍并调整设计。该情况下“缺失探测完成”验收仍未达成。

## 7. 第五步：扩大运行时对象覆盖

### 要达到的效果

读取 `window.innerWidth`、`performance` 的属性、`navigator` 的子对象等，不再因为创建路径不同而遗漏。

### 实施工作

- 按盘点清单逐组接入全局字段、performance、crypto、Storage、navigator 子对象、visualViewport、matchMedia 返回值及相关集合。
- 在创建路径中接入统一记录机制，为子对象分配身份，方法返回宿主对象时复用底层身份。
- 每组声明支持的 get/set/call/get_probe 操作；新对象不自动继承未实际实现的覆盖承诺。

### 注意事项

- 独立原生函数优先在其原生回调记录 call。
- 将普通数据属性改成 accessor 会改变描述符；Proxy 也会影响身份、this、品牌检查和不变量。先验证方案，再确定是否采用，不能作为全局默认包装。
- 返回对象可来自多个路径；对象身份可靠，源码路径通常不可恢复。

### 验收

每组对象都有开启／关闭监控的差分测试，包含描述符、原型、身份、返回值、异常与子对象。覆盖表逐项更新。

## 8. 第六步：补齐其他操作及诊断能力

### 要达到的效果

区分“读方法引用”“调用方法”“检查存在”“枚举”“查询描述符”和“修改属性定义”。

### 实施工作

- 分批设计 has、ownKeys、getOwnPropertyDescriptor、defineProperty、delete、方法属性 get 等事件，不将它们折叠为 get/set。
- 给现有 Reflect 通道补充目标对象关联的方案；无法识别的对象标 unknown。
- 增加可选位置／栈、基本类型参数摘要和返回摘要；异步来源优先保证正确，缺失就明确标缺失。
- 说明反射包装函数可能被脚本提前缓存或替换带来的盲区。

### 验收

相同属性名、不同目标对象可区分；读方法引用不等于调用；枚举返回顺序和描述符不变；元数据采集不会执行用户代码或吞掉原有异常。

## 9. 第七步：运行报告与使用流程

### 要达到的效果

用户能得到“访问清单 + 缺失候选 + 本次运行覆盖边界”，并知道下一步应检查什么。

### 建议流程

1. 开启追踪；若分析 load 内脚本，必须在 load 前开启。
2. 加载页面／执行命名脚本。
3. 按目标场景推进现有 tick/advance/run_until_idle，按需派发事件；给步骤和推进次数设置上限。
4. 即使目标 JS 抛错，也在 dispose 前读取已记录日志。
5. 输出按对象／属性聚合的计数，同时保留原始 seq 和首次来源。
6. 报告监控范围、未执行的交互、推进是否耗尽预算以及丢弃日志数。

### 注意事项

- 动态运行只覆盖本次执行的分支，日志不是所有可能依赖的静态清单。
- “没有日志”可能是未执行、未覆盖、过滤或容量截断，不能直接断言没有访问。
- 普通 JS 对象显式 watchObject、Proxy 或 AST 插桩作为后续独立方案；代理之外的旧引用仍可绕过监控。

## 10. 实施位置与验证要求

| 位置 | 预期职责 |
|---|---|
| `include/iv8/host_object.h` | 宿主身份／类别等必要元数据 |
| `src/core/host_object.cpp` | 统一 get/set/call；后续受控属性探测 |
| 新的原生追踪模块 | 事件结构、缓冲、序号、容量、过滤与生命周期 |
| `src/core/page_state.cpp` / `include/iv8/page_state.h` | Page 所有权、重载代次、其他对象安装路径 |
| `src/binding/page_binding.cpp` | 薄绑定和返回值转换 |
| `python/iv8/page.py` | 参数校验、公开 API 与说明 |
| `test/` | 聚焦行为测试、监控差分测试、生命周期测试 |

先跑改动涉及的聚焦 pytest；通过后完成项目要求的回归和真实 V8 Linux／Windows 验证。骨架构建的形状测试不能证明运行时监控有效。必须确认安装的扩展对应本次改动，不能以旧 wheel 的通过结果代替。

每个实现切片同步维护边界文档和 `runtime_surface.md`；实际落地后再更新 `current_status.md`、`next_tasks.md` 和有关决策。最终验收的关键是“日志有用、边界明确、原程序行为一致”，而不是日志条数越多越好。
