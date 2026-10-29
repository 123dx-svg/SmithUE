# SmithUE Changelog

## v1.16.0（UE5.2，2026-10-29）

### 修复：多编辑器并行隔离

- 服务每次启动生成独立 `instance_id`，由端口文件和 `/ready` 返回；接收端在路由到工具前核验请求头中的预期 PID / 实例标识，错配返回 HTTP 409，防止目标退出后端口复用将编辑请求送到新实例。
- 默认 HTTP 端口改为 `0`，由系统为每个编辑器分配独立端口；旧固定端口已被占用时自动回退，监听不再开启地址复用。端口文件继续按 PID 保存实际端口。
- `/ready` 返回 PID，供配套 CLI 核验实例身份。配套 CLI 使用独立 AI 会话绑定，不再根据全局最近连接记录猜测目标；目标退出时禁止自动切换工程。
- npm 安装与 SKILL 重装加入用户级原子维护锁，与配套 CLI 的升级 / 部署共用；npm 子进程继承锁所有权。锁竞争立即提示，不阻塞编辑器主线程。所有参与项目都需更新插件才能共同遵守锁协议。
- UE5.2 的 7 项检查器 Automation 测试通过，包含 UE / Node 锁互操作；真实双工程以相同固定端口并行启动时，验证独立端口、会话隔离和目标退出后拒绝回退。

### 修复：启动时检测并升级 CLI / SKILL

- CLI 环境检查新增有界 npm 最新版本查询，按线上最新版与兼容下限的较高者判断是否过期；网络失败时回退兼容下限，不再依赖每次 CLI 发版后修改插件常量。
- 新增默认开启的 `bAutoUpgradeCliOnStartup` 设置：检测到 CLI 未安装或过期且 npm 可用时，每次编辑器会话最多自动安装 / 升级一次，由 npm postinstall 部署配套 SKILL；可在项目设置的 SmithUE → Status & Updates 中关闭。
- SKILL 检测覆盖缺失的 references / reference / scripts 目录；重装改为复制完整技能包，并清理旧 reference 目录。状态面板显示线上版本，保留手动重装入口。
- 新增版本目标选择及过期判断回归测试，覆盖线上新版本、离线回退、无效版本与预发布版本；UE5.2 编辑器内 6 项 `SmithUECliChecker` Automation 测试通过。

## v1.15.0（UE5.2，2026-07-03）

### 新增：AnimGraph Phase 2 状态机编写工具

- **`bp_add_state_machine`**：在 AnimBlueprint 的 AnimGraph 中创建 `UAnimGraphNode_StateMachine`，返回 state-machine 节点 GUID 与内部 `UAnimationStateMachineGraph` 名称。实现镜像 `UAnimGraphNode_StateMachineBase::PostPlacedNewNode()`：由节点自身创建内部状态机图、设置 `OwnerAnimGraphNode`、调用 `UAnimationStateMachineSchema::CreateDefaultNodesForGraph()` 生成 Entry，并将子图挂到父 AnimGraph `SubGraphs`。
- **`bp_add_anim_state`**：在状态机图中创建 `UAnimStateNode`，返回 state 节点 GUID 与状态 `BoundGraph` 名称，供后续 `bp_create_node` / `bp_set_anim_node_property` 填充状态 Pose。实现镜像 `FEdGraphSchemaAction_NewStateNode::PerformAction()` + `UAnimStateNode::PostPlacedNewNode()`：节点自身创建 `UAnimationStateGraph`、用 `AnimationStateGraphSchema` 初始化默认 Result 节点并挂接子图；首个真实状态自动从 Entry 连入。
- **`bp_add_anim_transition`**：在两个状态之间创建 `UAnimStateTransitionNode`，返回 transition 节点 GUID 与规则 `BoundGraph` 名称，供后续创建条件逻辑。实现镜像 `UAnimationStateMachineSchema::CreateAutomaticConversionNodeAndConnections()`：用 `FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>()` 创建节点 / `UAnimationTransitionGraph`，再调用 `UAnimStateTransitionNode::CreateConnections(FromState, ToState)` 连线。
- **`bp_read_state_machine`**：只读配对工具，报告 states、transitions、Entry 指向、state/transition BoundGraph 名称；`state_machine` 参数支持 bp_add_state_machine 返回的 node_id 或 graph name。
- **自描述边界**：4 个工具描述均标明仅支持 AnimBlueprint state machines、返回图名供后续填充、图变更后 node ids 会 stale；错误信息会指向 `anim_read_blueprint` / `bp_read_state_machine` / 正确参数形态。
- **模块依赖**：Phase 1 已加入的 `AnimGraph` / `AnimGraphRuntime` 覆盖本阶段所用编辑器节点与 schema；本次未新增 Build.cs 依赖。

### 修复：`delete_asset` force=true 仅跳过预检，未真正强删

- **根因**：`HandleDeleteAsset` 的 `force` 参数只在 AssetRegistry 引用预检（~L785）处生效，最终删除始终调用 `ObjectTools::DeleteAssets(AssetsToDelete, false)`。该函数会在内部再次做内存引用检查，遇到有内存引用者（如 AnimBlueprint 被骨架/预览类/生成类持有）时返回 0，导致 `force=true` 实为"半强删"，实际并不删除。
- **修复**：当 `bForce == true` 且 `Asset` 有效时，改调 `ObjectTools::ForceDeleteObjects(TArray<UObject*>{ Asset }, /*bShowConfirmation=*/false)`（该函数强制删除并将内存引用者置空）；`force=false` 路径保持原有 `DeleteAssets` 逻辑不变。
- **签名确认**（`Engine/Source/Editor/UnrealEd/Public/ObjectTools.h:363`）：
  `UNREALED_API int32 ForceDeleteObjects( const TArray<UObject*>& ObjectsToDelete, bool ShowConfirmation = true );`
  返回值为已删数量，与原 `DeletedCount == 0 → error` 分支兼容。
- **描述更新**（§3.1）：`force=true force-deletes even with in-memory referencers, nulling them`，准确反映实际行为。
- **验收标准**：对含引用的 ABP_P1Test AnimBlueprint 执行 `delete_asset` + `force=true` 返回 `deleted: true`。

### 新增：AnimGraph Phase 1 节点属性 / 引脚 / 变量绑定工具

- **`bp_set_anim_node_property`**：通过反射写 `UAnimGraphNode` 内部 `FAnimNode` 结构体属性（如 SequencePlayer 的 `Sequence` / `PlayRate`），用于配置 AnimGraph 节点默认值。
- **`bp_expose_anim_pin`**：切换 `ShowPinForProperties` 中指定属性的可见性，暴露/隐藏 anim 节点属性引脚。
- **`bp_bind_anim_property`**：写入 `UAnimGraphNode_Base::PropertyBindings`，将 anim 节点属性绑定到 AnimBlueprint 成员变量（fast-path，无需连线；空变量名表示解绑）。
- **`bp_read_anim_node`**：只读检查指定 AnimGraph 节点的可设置属性、可选引脚和已有绑定，满足 create/mutate ↔ read 配对要求。
- **模块依赖**：`SmithUE.Build.cs` 新增 `AnimGraph`（`UAnimGraphNode_Base` / `FAnimGraphNodePropertyBinding` / `FOptionalPinFromProperty` 相关编辑器节点 API）与 `AnimGraphRuntime`（SequencePlayer/BlendSpace 等具体 runtime anim-node 结构所在运行时模块，供 AnimGraph 节点属性工具覆盖非 Engine 基础节点）。

### 修复：bp_create_node owner_class 参数 Schema 注册 + 端口文件 PID 存活剪枝

- **`bp_create_node`**：`target_class` 描述更新——明确支持原生类名或 `/Game/...` 蓝图资产路径（自动解析 `_C` 生成类），修复 Cast 节点目标类路径解析。VariableGet/VariableSet 新增可选参数 `owner_class`：当变量属于外部类（如 Cast 结果类）时指定宿主类；省略则默认解析当前蓝图（Self）。**注意：** 处理逻辑已在此前 C++ 提交中实现，本次为 Schema 注册补全（需重编译后 `list_tools` 才能广播该参数）。
- **端口文件剪枝**：`prune` 命令改为按 PID 存活状态判定废弃端口文件，修复多个编辑器实例复用固定端口时幽灵文件残留的问题。
- **`scripts/regen-tools.mjs`**：新增 `SMITHUE_PORT` 环境变量覆盖；需要从共享 / symlink 插件所在的其他宿主编辑器重生成 `TOOLS.md` 时，可直接指定端口，未指定时仍保持原有 AIScript 端口文件扫描行为。

## v1.13.0（UE5.2，2026-06-26）

### 新增：SKILL 漂移检测与一键重装（Status & Updates 面板）

- **`ESkillState` 枚举**（`Unknown / NotDeployed / Stale / Synced`）新增到 `FSmithUECliChecker`，`FCliInfo` 同步增加 `SkillState` + `SkillSourcePath` 字段。
- **`ComputeSkillState`**：CLI 环境探针完成后，对比本地已部署的 `~/.agents/skills/smithue-control/SKILL.md` 与全局已安装 `smithue-cli` 自带的 `skill/SKILL.md`（CRLF→LF 规范化后逐字节比较），判定是否漂移。结果写入 `LogSmithUE`。
- **`GetSkillState()` / `ReinstallSkill()`**：前者从游戏线程缓存读取状态；后者将已安装 CLI 的 bundle 复制到全部 agent 技能目录（`~/.agents`，以及存在时的 `~/.claude` / `~/.codex`），并在完成后自动触发重新探针。**注意：** 重装的是*当前已安装 CLI*的 SKILL，想要最新版请先升级 CLI。
- **`kRecommendedCliVersion` 升至 `0.13.4`**：旧 CLI 机器自动进入 *Outdated → 升级* 流程，升级触发 `postinstall` 重新部署最新 SKILL，构成漂移自愈闭环。
- **「Status & Updates」面板新增两行**：SKILL 状态文字（未检测 / ⚠未部署 / ↑已过期 / ✓已同步）+ **「重装 SKILL」按钮**（仅在 `NotDeployed` / `Stale` 时可见；含 tooltip 提示"想要最新先升级 CLI"，防止降级踩坑）。
- **`docs/spec/RELEASE.md §3.3`** 增加维护提示：每次 CLI 发版（尤其改 SKILL 时）须同步 bump `kRecommendedCliVersion`。

## v1.12.0（UE5.2，2026-06-26）

### 修复：材质工具自描述（对齐 TOOL_SPEC §3.1）
- **`connect_material_pins`** 的 `dest_input_index` 描述补全 `7=WorldPositionOffset`（此前只列 0–6；WPO/顶点偏移连不上只能翻源码 `case 7`）。8+ 标注不支持。
- **`set_expression_property`** 描述按节点类型列出合法 `properties` 键（Constant=`value` 而非 `R`、Constant3Vector=`r/g/b`、ScalarParameter、VectorParameter、TextureSample、Custom 等）；失败错误从无指向的 "No recognized properties were set" 改为**回显该节点的合法键**（新增 `GetSettablePropertyKeys()` 助手）。
- **`PITFALLS.md #15`** 沉淀该盲区；新增原则：description 写工具契约（索引、键名），不写 UE 版本特有的引擎 API（HLSL intrinsic 等），避免随版本漂移误导。
- `TOOLS.md` 已重生成（仅这两条描述变化，工具数不变 221）。

## v1.11.0（UE5.2，2026-06-25）

### 修复：工具描述隐藏边界 → AI 误判防护（Anti-Misjudgment）
- **`bp_compile_code`**：描述写明"只编译函数图、不支持事件/嵌套 if/裸 math"；`ValidateSyntax` 加窄事件守卫（仅检测签名行首 token `event`，或解析失败且含规范 UE 事件名），失败时返回重定向到原子节点工作流（`bp_override_function → bp_create_node → bp_describe_graph → bp_batch_op → bp_compile`），**不拦截**合法函数名（如 `void Tick()` 可正常编译）。
- **13 个工具描述补充边界信息**（全文字改动，无行为变更）：`bp_validate_code`（只读/不编译）、`bp_batch_op`（节点操作需 bp_path+graph_name）、`bp_get_class_members`/`bp_get_component_details`（枚举白名单）、`bp_focus_node`（互斥目标）、`create_data_asset`（需具体子类）、`add_widget`（需面板父级）、`bp_create`/`bp_create_node`/`bp_connect_pins`/`bp_disconnect_pins`（易混对消歧）、`bp_create_node`/`bp_delete_node`（nid_stale 警告）、`level_new`/`level_open`（延迟执行）。
- **附带修复**：`CompileFunction` 中 `NewResult->AllocateDefaultPins()` 被重复调用（`Finalize()` 后再次显式调用），导致 FunctionResult 节点有两个 `execute` 引脚，`FindPin("execute")` 歧义返回 nullptr，所有函数体 exec 连接静默失败。已移除多余的显式调用。
- **`docs/spec/TOOL_SPEC.md §3.1`** 新增强制规范：description 必须在被挑选时暴露硬边界，常见误用错误必须可操作，沉淀为长期开发门控。
- **`docs/spec/PITFALLS.md #14`**：记录本次踩坑；**#13** 更新为正确的 `NormalizeContentBrowserPath` 指引（旧版 `/All` 字符串截断对插件路径错误）。
- **`TOOLS.md`** 已从 `/api/v1/tools` 重新生成（14 个工具描述变更，工具总数不变 221）。

### 新增：启动环境自检 + Status & Updates 设置面板
- **`FSmithUECliChecker`**：编辑器启动后后台检测 Node / npm / smithue-cli 环境（off game thread，写回 game thread，每步写入 `LogSmithUE`）。语义化版本比较 + 兼容地板 `kRecommendedCliVersion=0.13.0`。
- **「Status & Updates」设置面板**（项目设置 → 插件 → SmithUE）：查看环境状态、插件更新提醒（GitHub Releases 链接 + Restart-to-Update 按钮）、一键安装/升级 CLI；按钮随状态自适应（未安装/升级/已最新/取消），Node 缺失时给 nodejs.org 链接。拉取式（TAttribute）刷新，无订阅。
- **有界安装**：CLI 安装走 `CreateProc` + 管道 + 轮询，**120s 硬超时 + 可取消 + npm 快速失败参数**（`--fetch-timeout=60000 --fetch-retries=1`），网络不佳不假死；失败分类提示（权限/网络/超时）+ 手动兜底；安装与检测使用独立 in-flight 标志。

### 变更
- 启动时不再弹更新 toast；插件更新提醒移入设置面板。`level_new` 等不变。

### 修复：Content Browser 选区/路径返回虚拟路径
- **`get_content_browser_selection`** 的 `selected_folders` 此前直接返回引擎虚拟路径（`/All/Game/BP`、`/All/Plugins/Foo/BP`），下游无法直接当作包路径使用。现统一规范化为真实包路径（`/Game/BP`、`/Foo/BP`），并新增 `selected_folders_virtual` 字段保留原始虚拟路径用于调试。
- 新增 `FSmithUECommonUtils::NormalizeContentBrowserPath()`：基于官方 `UContentBrowserDataSubsystem::TryConvertVirtualPath` 转换（同时正确处理工程 `/Game` 与插件挂载路径），失败时回退（`/All`→`/Game`、`/All/Game..` 截断），绝不静默丢弃。
- 合并此前散落在 4 处的 `/All` 字符串截断逻辑（`SmithUEBpAtomicAPI.cpp` ×3、`SmithUEAssetAuditCommands.cpp`）统一改调用该 helper；`SmithUE.Build.cs` 新增 `ContentBrowserData` 模块依赖。

## v1.10.0

### 新增工具（合计 221 工具 / 24 个领域）
- **`get_asset_property`**：通用 UObject 属性读取器（set_asset_property 的读对偶）
- **`scan_assets`**：文件夹作用域资产扫描，返回 v1 linter 元数据（命名/路径/父类/材质槽/LOD/碰撞）
- **`bp_describe_components`**：Blueprint 组件树读回，供合规 linter 比对（继承盲区显式标注）
- **`spawn_mesh_actor`**：向当前世界生成带网格 + 材质的 StaticMeshActor（补 spawn_actor 无法赋网格/材质的缺口）
- **`level_add_basic_env`**：向当前世界一键添加基础环境（定向光 / 天空 / 天光 / 雾 / 地板 / PlayerStart）
- **`livecoding_status` / `livecoding_compile`**：Live Coding 热编译状态查询与同步触发（新增 LiveCoding 领域）

### 修复
- **`level_new` 崩溃修复**：将同步 `NewBlankMap` 改为经 `GEngine->DeferredCommands` 延迟到下一帧安全点执行，避免在 HTTP handler 内 tick 期间销毁世界导致崩溃（复刻 level_open 的延迟模式）。
- **材质 WorldPositionOffset 输出引脚**：`connect_material_pins` 的 `dest_input_index=7` 正确映射到 WPO 引脚。

### 架构演进
- 企业级资产装配与合规标准化引擎定位正式确立
- 三层分离：C++ 原子原语 / smithue-cli 规范引擎 / 宿主工程 spec 文件
- `docs/usage/` 使用文档 hub 建立（与 `docs/spec/` 开发文档拆分）

## v1.8.0 — Blueprint Breakpoint & Navigation

### Added
- `bp_set_breakpoint` — Set/enable a breakpoint on a Blueprint node by NodeGuid. Params: `bp_path` (required), `graph_name` (EventGraph or function name), `node_id` (NodeGuid), `focus` (boolean, default true). When `focus=true` (default) also opens the Blueprint editor and jumps to the node. Set `focus=false` for headless/batch use. Returns `{bp_path, graph, node_id, node_title, enabled}`.
- `bp_clear_breakpoint` — Remove a breakpoint from a node. Same params as `bp_set_breakpoint` (including `focus`). Returns `{..., was_present}`.
- `bp_list_breakpoints` — List all breakpoints in a Blueprint. Param: `bp_path` (required). Returns `{bp_path, breakpoint_count, breakpoints:[{graph, node_id, node_title, enabled}]}`.
- `bp_focus_node` — Open a Blueprint editor and focus a target. Params: `bp_path` (required) + exactly one of: `node_id`+`graph_name` (jump to node), `function_name` (open function graph), or `variable_name` (select in My Blueprint panel).

### Enhanced
- `bp_describe_graph` and `bp_search` node output now include a `node_guid` field (the real NodeGuid) alongside the existing `id`/`nid` short alias. Use describe/search to find a node, read `node_guid`, then pass it directly to `bp_set_breakpoint` or `bp_focus_node`.

## v1.7.0 — Blueprint Troubleshooting Trio

### Added
- `bp_health_check` — aggregate Blueprint diagnostics in one call: compile errors/warnings, unconnected required pins, broken member references, and orphan (unreachable) nodes; returns an overall `healthy` verdict. Token-controlled via `checks` filter + `limit`.
- `bp_diff` — structural comparison of two Blueprints across parent class, components, variables, functions, interfaces, and overrides; reports only_in_a / only_in_b / differs per aspect. Automates the "broken BP vs reference BP" comparison workflow.
- `bp_trace_value` — data-flow trace from a node pin, following non-exec links upstream (what drives a value) or downstream (what it feeds), bounded by `max_depth` with cycle guards.

> Note: implemented and compile-verified; live runtime verification pending an editor reload (UE recompiles the plugin DLL on next launch).

## v1.6.0 — Blueprint Component Property Inspection

### Added
- `bp_get_component_details` command — read each component's template reflected properties for a Blueprint: Mobility, relative transform (location/rotation/scale), absolute flags (bAbsoluteLocation/Rotation/Scale), visibility, and for primitives Simulate Physics, gravity, collision profile/enabled, mesh asset, and material overrides.
  - Covers BOTH the Blueprint's own SCS components AND inherited components (via the generated-class CDO), de-duplicated by name; each entry tagged `source: scs|inherited`.
  - Token-conscious controls: `component` (filter to one), `props` (group filter: transform,mobility,physics,rendering,mesh,collision), `include_inherited` (default true).
  - Closes the troubleshooting gap where `bp_get_summary` showed only the component hierarchy — now Mobility/Absolute/Physics that drive "component stuck at origin / not following actor" issues are directly readable.

## v1.5.0 — Blueprint Class Member Inspection

### Added
- `bp_get_class_members` command — inspect a Blueprint or native C++ class's members (functions, variables, macros, delegates, interfaces) with full inheritance-chain attribution.
  - Returns `inheritance_chain` (each entry tagged blueprint/native + module/blueprint_path), up to UObject.
  - Members grouped by owning class — distinguishes which class in the chain declares each function/variable/interface/delegate.
  - Token-conscious controls: `scope` (self|chain|owner:<Class>), `kinds` filter (functions,variables,macros,delegates,interfaces), `detail` (compact|full), `limit` (default 200, sets `truncated:true` when exceeded). `counts` always reports full per-owner totals cheaply so callers can size queries before drilling in.
  - Resolves both Blueprint asset paths and native C++ class names (e.g. `ACarPawn`).

## v1.4.0 — HTTP Robustness

### Fixed
- **Portfile self-healing**: FTSTicker heartbeat (4s interval) re-writes portfile if deleted externally. Prevents permanent "No portfiles found" after CLI bug or antivirus deletion.
- **HTTP concurrency**: Replaced single-thread serial accept loop with bounded worker pool (max 4). `/ready` and lightweight commands now respond immediately even while long game-thread commands execute.
- **Safe shutdown drain**: StopServer() waits up to 2s for in-flight workers before deleting portfile. Prevents use-after-free and ensures portfile is removed cleanly.
- **Thread-safety**: `/ready`, `ping`, `list_tools`, `get_protocol_info` audited and confirmed safe to execute on worker threads without game-thread access.

---

## v1.3.0 — Blueprint Component System Enhancement

### Added
- `bp_add_component` supports `parent` parameter for hierarchical component attachment.
- `bp_remove_component` command for removing components from SCS; child components auto-re-attach.
- `bp_get_summary` returns component hierarchy with `parent` and `children` fields.
- `bp_create` supports three parent class formats: C++ class name, Blueprint class name with `_C` suffix, or Blueprint asset path.
- `bp_create_node` supports `Class::Function` shorthand for automatic CallFunction node creation.
- `bp_create_node` supports `K2Node_EnhancedInputAction` (via `input_action` parameter).
- `bp_create_node` supports `K2Node_DynamicCast` (via `target_class` parameter).
- Added `InputBlueprintNodes` module dependency for Enhanced Input Blueprint nodes.

### Fixed
- Fixed Blueprint component recompile crash by using `SkipGarbageCollection`.
- Fixed pure virtual function crash when adding Blueprint-derived components to SCS.

---

## v0.8.0 — CLI Migration

- Removed TCP Server / ConnectionManager / SessionManager.
- HTTP Server switched to dynamic port + portfile discovery.
- `/ready` endpoint + startup 503 guard.
- StatusIndicator rewritten as CLI-aware dot + copy CLI command button.
- New `smithue-cli` npm package replaces MCP: `npm install -g smithue-cli`.

---

## v0.6.0 — N-id Sessions, Metrics, Blueprint Preview

### Added
- N-id session system: short ID (N0, N1, ...) to GUID mapping per Blueprint graph, with expiry detection.
- Command metrics: call counts, request/response bytes, execution time, retry detection, per-command stats.
- `system_get_metrics` / `system_reset_metrics` commands.
- `take_blueprint_preview_screenshot`: capture any Blueprint's SCS viewport as PNG.
- Editor state guard: reject non-read-only commands while PIE is running.
- Blueprint atomic API expansion: ~1600 lines of new Blueprint operation primitives.

### Fixed
- Fixed `FTSTicker::FDelegateHandle` type for UE 5.2+ compatibility.
- Added `RHI` module dependency in Build.cs.
