# SmithUE Changelog

## v1.16.4（UE5.8，2026-08-31）

### smithue-cli 自动升级：不再需要用户点按钮

原来有两个独立问题叠加，导致"自动检测"实际上永远要人工干预：

1. **判定基准是写死的常量**。`kRecommendedCliVersion` 是插件源码里的字面量，
   CLI 一发新版它立刻过期 —— 装着 0.15.0 的机器在 CLI 已发 0.16 之后
   仍然显示 `✓ Ready`，**永远不会提示升级**。要生效必须改插件源码并重新发插件。
2. **检测出来了也只是"显示"**。`Outdated` 只是把按钮点亮，没有任何自动执行路径。

改法：

- **查真实的 npm latest**：探测阶段新增 `npm view smithue-cli version`
  （`RunBounded`，20s 硬超时，失败/离线容错）。目标版本 =
  `max(插件写死的下限, npm latest)`。写死的常量退化为**兼容性下限**
  （插件确实需要更新 CLI 时才抬），不再是发版标记 —— **CLI 发版不再需要动插件**。
- **自动执行升级**：探测完成后在 game thread 走 `MaybeAutoUpgradeCli()`，
  命中 `Outdated` / `NotInstalled` 就直接调 `ExecuteCliInstall()`，并弹 toast 告知。
  刻意保守：**每个编辑器会话最多一次**、必须已开启开关、必须探测到可用的 npm。
  一次成功安装会回调 `CheckCliEnvironment()`，`GAutoUpgradeAttempted` 同时兼作
  防重入哨兵，避免"升级 → 重查 → 再升级"的自激循环。
- **新开关** `bAutoUpgradeCliOnStartup`（Project Settings → Plugins → SmithUE →
  Status & Updates，默认开，依赖 `bCheckCliOnStartup`）。
  它会改动机器的**全局 npm 包**，共享机 / 受控环境 / 版本被策略钉死的场合应关掉。
- **面板补强**：状态行增加 `(npm latest X)`；安装按钮在 `Ready` 状态下**不再置灰**，
  变为「强制重装」—— 此前健康机器没有任何途径重装 CLI 或重推 SKILL。

新增纯函数 `SmithUECliInternal::ResolveTargetCliVersion` / `IsCliOutdated`
（放公共头 + `SMITHUE_API`，遵循本仓库"纯逻辑可测"的约定），
配套 Automation 测试 `SmithUECliChecker.ResolveTargetCliVersion` /
`SmithUECliChecker.IsCliOutdated` 覆盖：latest 高于/低于/等于下限、离线无 latest、
npm 吐垃圾、预发布版不得压过正式版、以及"无可信目标时绝不误判 outdated"
（否则断网机器每次启动都会重装）。

**端到端实测**：把全局 CLI 降到 `0.14.1` → 启动编辑器 → 未做任何点击：
```
[SmithUE CLI] npm view probe: rc=0 timedout=0 out='0.15.0'
[SmithUE CLI] auto-upgrade: state=2 installed='0.14.1' target='0.15.0'
[SmithUE CLI] install: rc=0 timedOut=0 cancelled=0 out='changed 7 packages in 2s'
[SmithUE CLI] skill state: 3   (Synced)
```
`npm ls -g` 确认已回到 `0.15.0`，且第二轮探测**没有**再次触发 auto-upgrade（防重入生效）。

### 未做：编译期（UBT）钩子
"项目编译时升级"需要把 npm 调用塞进 `SmithUE.Build.cs`（跑在 UBT 的 C# 进程里）。
没有做，因为它会：每次编译都联网、拖慢并可能卡住构建、在 CI / 离线 / 构建农场上
破坏可重复性，且出错时没有 UI 可反馈。编辑器启动钩子覆盖了同样的场景
（插件重编译后必然重启编辑器）。如确需编译期升级，应作为显式的一次性脚本，而非 Build.cs 副作用。

## v1.16.3（UE5.8，2026-08-31）

### 修复：只能读到节点自身信息，读不到节点"引用了什么"
此前 `bp_describe_graph` / `bp_search` 只序列化 `UEdGraphNode` 的通用表面（class / title / 位置 / 引脚），
节点类自己的 UPROPERTY 全部丢失 —— 于是 `K2Node_CallFunction` 看不到调用了哪个函数、
`K2Node_VariableGet` 看不到读的是哪个变量、`K2Node_MacroInstance` 看不到宏来自哪个蓝图、
PropertyAccess 类节点（`K2Node_GetEditorProperty`/`SetEditorProperty`，均派生自 `K2Node_CallFunction`）
也拿不到其引用属性。只能靠 `title` 猜。

- **新增每节点 `refs` 对象**（`SmithUEBlueprintCommands.cpp`）：通过反射遍历节点类中**声明在 `UEdGraphNode` 之下**的
  属性，只导出与 CDO 不同的"引用型"值。**无需按节点类逐个特判**，新节点类型自动覆盖，也不新增模块依赖。
  - `FMemberReference` 展开为 `{member_name, member_parent, self}` → `FunctionReference` / `VariableReference` /
    `MacroGraphReference` 等一次性全覆盖。
  - 对象/类引用、`FSoftObjectPath`/`FSoftClassPath` 输出路径；FName/FString/FText/枚举/bool 直出。
  - 数组、Set、**TMap** 递归；未识别的结构体递归取其引用型字段（AnimGraph 的 `PropertyBindings`
    即 `TMap<FName, FAnimGraphNodePropertyBinding>`，由此免 AnimGraph 模块依赖地浮出）。
  - 节点自持的子对象（如 `AnimGraphNodeBinding`）递归展开，而不是打印无意义的内部对象路径。
- **噪声控制**（否则 AnimGraph 节点会灌爆输出）：跳过 `CPF_Transient` / `CPF_Deprecated`；跳过引脚可见性元数据
  `FOptionalPinFromProperty`（信息已由引脚列表表达）；嵌套层级（depth ≥ 1）只保留真正的引用型叶子，
  丢弃 bool/枚举/数值这类设置项；递归深度上限 4，字符串上限 512。值等于 CDO 默认值即跳过，
  因此 `refs` 为空时整个字段省略，token 成本接近 0。
- **`bp_describe_graph` 新增 `include_refs` 参数**（默认 true，`summary` 模式默认 false）。
- `bp_search` 的匹配节点同样带 `refs`。
- **`K2Node_AddComponent` 的组件模板**：其资产引用不在节点上，而在蓝图 CDO 的组件模板里（节点只存
  `TemplateName`），改为解析 `GetTemplateFromNode()` 并输出 `refs.component_template`
  → 例如 `{"StaticMesh":"/Game/Vehicles/SportsCar/SM_SportsCar_Wheel"}`。此前只能从 title 字符串里猜。
- **CDO 里就设好的引用不再被过滤**：`FMemberReference` 属性豁免"等于默认值即跳过"规则 ——
  `K2Node_GetEditorProperty`/`SetEditorProperty` 在构造函数里就写死了 `FunctionReference`，
  否则恰恰是要查的那个字段被吞掉。

### 已知限制（未改）
- `bp_create_node` 对 `UK2Node_CallFunction` 的**所有子类**都强制要求 `function_name`，
  但自配置型子类（`K2Node_GetEditorProperty` 等）构造函数里已设好 `FunctionReference`，
  不传就返回 "Failed to create node"。**已在本版修复**（见下）。

### 修复：`bp_create_node` 拒绝自配置型 CallFunction 子类
`SmithUEBpAtomicAPI.cpp` `CreateNode`：`function_name` 改为**可选** —— 仅当未提供且
`FunctionReference` 也为空时才失败。`K2Node_GetEditorProperty` / `K2Node_SetEditorProperty`
等在构造函数里 `SetExternalMember` 的子类现在可直接创建。

### 修复：一批蓝图**读取盲区**（深度审计产物）
`bp_describe_graph` / `bp_search` 之前只输出 class/title/位置/引脚基本信息，
下列会**改变逻辑解读结论**的信息全部缺失：

- **节点状态**（此前被 `refs` 的"跳过 UEdGraphNode 基类"规则连带滤掉）：
  - `enabled`：`disabled` / `development_only` —— 被禁用的节点不参与运行，
    此前会被当成生效节点读，结论直接错。
  - `comment`：节点注释气泡文字（作者意图的唯一记录）。
  - `error`：`bHasCompilerMessage` + `ErrorMsg`，有编译报错的节点此前完全看不出。
  - `size`：注释框（`EdGraphNode_Comment`）的 `NodeWidth/NodeHeight` ——
    注释框靠几何范围分组节点，没有尺寸就无法判断哪些节点属于哪个分组。
- **引脚结构标记**：
  - `parent` / `split_into`：**拆分结构体引脚**。此前父引脚显示一个**过期的** default
    （实测 Ultra_Dynamic_Weather 里父引脚 `A` 报 `1800,0,0`，真值其实来自
    `A_X ← N10.ReturnValue`），且父引脚在 compact 模式被当作"无默认值无连线"整个丢弃，
    数据流直接断链。现在父引脚不再被 compact 丢弃，并标出拆分关系。
  - `orphaned`：函数签名变更后遗留的孤儿引脚 —— 排故关键，且不再被 compact 过滤。
  - `hidden` / `advanced`：此前 full 模式把隐藏引脚和普通引脚混在一起无从区分。
  - `label`：`PinFriendlyName`（UI 名 ≠ 内部名，如 `bAlphaBoolEnabled` → "bEnabled"）。
- **图引用**：`refs` 中的 `UEdGraph*` 属性（`K2Node_Composite::BoundGraph` 等）
  改为输出**图名**，可直接拿去 `bp_describe_graph` 下钻；此前会递归进该图的所有节点产生垃圾输出。

实测（Ultra_Dynamic_Weather，306 图 / 6387 节点）：89% 节点带 `refs`，
命中 110 个注释框、8 个拆分引脚、3032 个隐藏引脚、211 个高级引脚、5068 个 UI 别名。

### 仍未修（已确认存在，待后续）
- 无。上一轮列出的 4 项已在本版全部修复（见下）。

### 修复：`bp_get_summary` 的四类资产级读取盲区
- **变量默认值 / 分类 / 完整标志位**：此前变量只有 `name` + `type` + `read_only`/`replicated`，
  读不到任何配置值。现在补 `default`（**从 GeneratedClass CDO 反射导出**，而不是用
  `FBPVariableDescription::DefaultValue` —— 后者只对简单字面量有效，结构体/对象类型会过期）、
  `category`（用 `UEdGraphSchema_K2::VR_DefaultCategory` 比较，避免中文本地化的"默认"漏判）、
  以及 `editable`/`transient`/`save_game`/`config`/`expose_on_spawn`。
  `()` 与 `None` 视为未设置直接省略，控制 token。
- **函数图局部变量**：`UK2Node_FunctionEntry::LocalVariables` 此前在任何工具里都不可见，
  现在作为函数条目的 `locals` 输出（与变量同结构）。
- **Timeline**：Timeline 不是 `UEdGraph`，因此从来不出现在任何图列表里。
  新增 `timelines`：`name` / `length` / `auto_play` / `loop` / `replicated` /
  `tracks`（float / vector / linear_color / event 四类轨道 + 各自的曲线资产路径）。
- **折叠子图**：递归遍历所有 Ubergraph / Function / Macro / Delegate 图的 `SubGraphs`，
  输出 `sub_graphs`（`name` / `node_count` / `parent_graph`）。此前虽可按名 `bp_describe_graph`
  下钻，但**没有任何地方告诉你它存在**。

实测（全工程 100 个蓝图 / 94 个成功解析）：
1849 个变量中 **1506 个吐出默认值**、1636 个带分类、15 个 `expose_on_spawn`、77 个 `replicated`；
1108 个函数中 **196 个含局部变量，共 626 个**；命中 1 个折叠子图
（`Offroad_CtrlRig :: SequenceAggregate_SubGraph`）。改前这些数字全为 0。
Timeline 用 `bp_add_timeline` 现场建了一个验证读取（`{"name":"FadeTimeline","length":5}`）。

### 修复：Timeline 曲线关键帧读不到（黑盒）
Timeline 的曲线通常是**内嵌在蓝图里的对象**（`/Game/…/BP_Test.BP_Test_C:CurveFloat_0`）
而非独立资产，`UEditorAssetLibrary::LoadAsset` 解析不到 → `read_curve` 直接报
"Curve not found"。结果是只知道"有一条 float 轨道"，**它到底怎么插值、数值怎么变完全不可见**。

- **`bp_get_summary` 内联关键帧**：每条 track 增加 `keys`，含 `t` / `v` / `interp`
  （`constant` / `linear` / `cubic_auto` / `cubic_user` / `cubic_break`），
  cubic 且切线非零时附 `arrive` / `leave`。vector 轨道按 `{x,y,z}` 分量、
  linear_color 按 `{r,g,b,a}` 分组。单曲线上限 64 个 key。
  新增 `include_curve_keys` 参数（默认 true）。
- **`read_curve` 支持内嵌曲线**：`LoadAsset` 失败时回退
  `LoadObject<UCurveBase>`，从而解析 `Package.Object:SubObject` 形式的路径 ——
  即 `bp_get_summary` 里 `timelines[].tracks[].curve` 直接可拿去精读。

实测（用户在 `BP_Test` 里手工添加的 Timeline）：
```
"timelines":[{"name":"时间轴","length":5,"auto_play":true,
  "tracks":[{"name":"新建轨道_0","kind":"float",
    "curve":"/Game/SimFramework/Test/BP_Test.BP_Test_C:CurveFloat_0",
    "keys":[{"t":0,"v":0,"interp":"cubic_auto"},
            {"t":1,"v":0.3290833,"interp":"cubic_auto"}]}]}]
```
同一路径喂给 `read_curve` 也已可读（改前报 "Curve not found"）。
该样本同时补测了上一轮标注为"未实测命中"的三条：`tracks`、节点 `enabled:"disabled"`、
节点 `comment`（用户禁用了两个事件节点）。

### 文档
- `TOOLS.md` 由 `/api/v1/tools` 重新生成（顺带修正此前遗留的漂移：301 → **314 工具 / 29 域**），
  README / README.en / `.uplugin` 计数同步。

## v1.16.2（UE5.8，2026-07-28）

### 修复：SKILL 部署只拷了 SKILL.md，漏掉 reference/ 与 scripts/
- **`ReinstallSkill()`**：面板「重装 SKILL」按钮改用 `CopyDirectoryTree` 递归拷贝 CLI 自带的**整个** skill/ bundle（SKILL.md + reference/ + scripts/），不再只拷单个 SKILL.md。
- **`ComputeSkillState()`**：SKILL.md 内容匹配后，额外检查已部署目录是否含 reference/ 与 scripts/；缺失则判为 *Stale*，让「重装 SKILL」按钮出现 —— 使 0.14 之前只部署了单文件的**存量机器能自愈**。
- 配合 smithue-cli **0.14.1**（其 `postinstall` 与 `skill --install` 亦已改为部署完整 bundle）。

## v1.16.1（UE5.8，2026-07-28）

### CLI 同步与文档（无工具增减、无引擎 API 改动）
- **`kRecommendedCliVersion` 0.13.4 → 0.14.0**（`SmithUECliChecker.cpp`）：跟随已发布的 smithue-cli 0.14.0（SKILL 重构）。旧 CLI 机器将在「Status & Updates」面板显示 *Outdated → 升级*，升级即触发 CLI `postinstall` 重新部署最新 SKILL —— 保持 SKILL 漂移自愈。
- `docs/spec/PITFALLS.md`：#7 补充消费端 JSON 传参官方解法（`--params-file` 首选 → skill 自带 `scripts/smithue.ps1` → `smithue-exec.mjs`；CJK 关联 CLI 0.14.0 的 `charset=utf-8` 修复）；#12 更新 npm 发布现状（CI 自动发布 / `NPM_TOKEN` secret / email-OTP vs TOTP / bypass-2fa token 废弃 → Trusted Publishing OIDC）。
- `docs/UE5.8-UPGRADE-TEST-REPORT.md`：脚注说明 CLI 已独立升至 0.14.0（与引擎升级无关），历史结论保留。

## v1.16.0（UE5.8，2026-07-22）

### 升级到 Unreal Engine 5.8
- **API 兼容适配**（详见 [docs/UE5.8-UPGRADE-TEST-REPORT.md](docs/UE5.8-UPGRADE-TEST-REPORT.md)）：
  - `UMaterialExpression::GetInputs()` → `CountInputs()` + `GetInput(i)`；`GetMaterialResource(ERHIFeatureLevel)` → `GetMaterialResource(EShaderPlatform)`（SP_PCD3D_SM5/SM6）。
  - `#include "Engine/UserDefinedStruct.h"` → `StructUtils/UserDefinedStruct.h`。
  - `FJsonObject::Values` 的 key 变为 `UE::TSharedString<TCHAR>`（`.ToView()` 适配）。
  - `UAnimGraphNode_Base::PropertyBindings` 迁入引擎私有类 → 纯反射访问（不碰私有头）。
  - `EAutomationTestFlags` scoped 化（`ApplicationContextMask` → `EAutomationTestFlags_ApplicationContextMask`）；`.uplugin` `WhitelistPlatforms` → `PlatformAllowList`；`ue_version` 5.2 → 5.8。
- **弃用告警清零**：`SetMaterialUsage` 虚函数、`BL_*` 重命名、`ForEachObjectWithOuter` `EGetObjectsFlags`、`UE::IsSavingPackage()`、移除 `REN_ForceNoResetLoaders`、Sequencer `GetBindings` const + `FMovieScenePossessable/Spawnable` 取名（顺带修 5.8 binding 名字返回空的功能回归）。

### 新增域与工具（→ 301 工具 / 29 域）
- **AI 域（23，全链路编排 + CRUD）**：
  - **黑板**：`create_blackboard`/`blackboard_add_key`/`blackboard_remove_key`/`read_blackboard`。
  - **行为树**：`create_behavior_tree`/`bt_set_blackboard`/`bt_add_node`/`bt_read_node`/`bt_set_node_property`/`bt_add_decorator`（UBTDecorator 子节点）/`bt_add_service`（UBTService 子节点）/`bt_remove_node`（RemoveNode + 运行时重建，护 Root）/`read_behavior_tree`。
  - **EQS**：`create_eqs`/`eqs_add_option`（生成器）/`eqs_add_test`（UEnvQueryTest 子节点，挂 UEnvQueryOption）/`eqs_remove_option`/`read_eqs`。
  - **状态树**：`create_state_tree`/`state_tree_add_state`（EditorData builder + 重编译）/`state_tree_add_transition`/`state_tree_remove_state`/`read_state_tree`。
- **PCG 域（9，含 set_pcg_node_property/read_pcg_node）**：`create_pcg_graph`/`read_pcg_graph`/`add_pcg_node`/`connect_pcg_nodes`/`find_pcg_graphs`/`spawn_pcg_volume`/`pcg_generate`。
- **Dialog 域（3，worker-safe）**：`get_active_dialog`/`dismiss_active_dialog`/`set_dialog_auto_response`——基于 `FSlateApplication::OnModalLoopTickEvent`，模态框卡死 game thread 期间仍能识别与关闭弹窗；`/ready` 加 `modal_active`。
- **蓝图接口**：`create_blueprint_interface` + `bp_implement_interface`。
- **UMG**：`bind_widget_event`（绑定 Button.OnClicked 等委托事件，自动提升控件为变量并建事件节点）；`ResolveWidgetClass` 加反射兜底（支持 ProgressBar/Slider/CheckBox/ComboBoxString 等任意 `UWidget` 子类）。

### 新增域与工具
- **新域 Mesh(5 工具)— 静态网格构建操作**(`set_asset_property` 反射覆盖不到的构建类,基于 `UStaticMeshEditorSubsystem`)：
  - `read_mesh_info {mesh_path}`：读构建状态(LOD 数 + screen sizes、材质槽数、简单碰撞体数、Nanite 开关、LOD0 顶点数)——网格构建工具的断言companion。
  - `mesh_add_collision {mesh_path, shape}`：加简单碰撞体(box/sphere/capsule/ndop10x/ndop18/ndop26,自动贴合包围盒)。
  - `mesh_remove_collision {mesh_path}`：清空所有简单碰撞体。
  - `mesh_set_nanite {mesh_path, enabled}`：开/关 Nanite(重建网格)。
  - `mesh_generate_lods {mesh_path, lod_count}`：自动生成 N 级 LOD(逐级三角形减半,screen size 自动)。
  - 实测(SM_Door 副本):read→加 box+sphere 碰撞(0→1→2)→清空(→0)→Nanite 开/关→生成 4 级 LOD(screen sizes [2,0.75,0.56,0.42]),错误均拦截。Build.cs 加 `StaticMeshEditor` 依赖。

### 优化与修复
- **UMG 补强(2 工具)— 布局(Slot)属性 + 删除**：填补"能加控件但改不了布局、删不了控件"的缺口(Niagara 域 17 工具已较完整)。
  - `set_widget_slot_property {blueprint, widget, property, value}`：改控件 **Slot** 上的布局属性(布局在 Slot 上,不在控件上)。Canvas 用 `LayoutData`(内含 Offsets 位置/大小、Anchors、Alignment)/`ZOrder`;Box 用 `Padding`/`HorizontalAlignment`/`Size` 等。属性不存在时报错并列出该 Slot 类的可编辑属性名。
  - `remove_widget {blueprint, widget}`：从控件树删控件(连子级),护根控件。
  - 实测:VerticalBox slot(Padding/HAlign)、CanvasPanel slot(LayoutData 位置+大小+锚点+对齐 / ZOrder)、删除+缺失/根拦截,全通。
- **Mesh 域补强(2 工具)— 材质槽 assign + 骨骼网格读取**：
  - `mesh_set_material {mesh_path, slot, material_path}`：给静态或骨骼网格的材质槽赋材质(slot 支持索引或槽名);静态用 `UStaticMesh::SetMaterial`,骨骼改 `GetMaterials()[i].MaterialInterface`。实测静态槽0 + 骨骼按槽名 `M_HeadLegs`,坏 slot/材质拦截。
  - `read_skeletal_mesh_info {mesh_path}`：读骨骼网格状态(材质槽 name+材质、skeleton、physics asset、LOD 数、socket 数)。实测 SKM_Manny_Simple → skeleton SK_Mannequin / physics PA_Mannequin / LOD 3 / 5 sockets / 2 槽。
- **Blueprint 函数签名 / 局部变量 / Timeline(4 工具)**:补齐"函数创建后改签名、加局部变量、加 Timeline"缺口。
  - `bp_add_function_parameter {bp_path, function_name, param_name, param_type, direction}`：给已有函数加输入/输出参数(改签名),output 无 Result 节点时自动建;拦截重名。
  - `bp_remove_function_parameter {bp_path, function_name, param_name}`:按名删函数参数(自动判定输入/输出)。
  - `bp_add_local_variable {bp_path, function_name, var_name, var_type, default_value?}`:给函数图加局部变量(`FBlueprintEditorUtils::AddLocalVariable`);**显式拦截重名**(引擎 API 不查,重名会破坏编译)。
  - `bp_add_timeline {bp_path, timeline_name}`:加 Timeline 模板/组件(`AddNewTimeline`),拦截重名;轨道与驱动节点(K2Node_Timeline)另加。
  - `ResolvePinType` 增加小写结构体别名(vector/rotator/transform/color/vector2d,免写 `F` 前缀,与 PCG 参数一致)。
  - 实测:输入+输出参数增删、局部变量(含 vector)增+重名拦截、Timeline 增+重名拦截,编译零错。
- **修复 `spawn_pcg_volume` 无 brush 几何 → PCG 生成 0 点**:程序化 `SpawnActor` 出的 `APCGVolume` 没有 brush,采样体退化。现按 `UActorFactory::CreateBrushForVolumeActor` 流程构建 200³ 盒体 brush(`UCubeBuilder::Build` + `FBSPOps::csgPrepMovingBrush`),再套 actor scale。实测 VolumeSampler 生成 **8000 点**(scale 10 → 2000³ 体,VoxelSize 100),`pcg_get_stats` 正确读出——PCG 端到端生成测试闭环。Build.cs 加 `BSPUtils` 依赖。
- **PCG 图级用户参数(3 工具)— 完整生命周期**:补齐图暴露参数的脚本化编辑(此前只能在编辑器 UI 手动加)。
  - `add_pcg_param {graph_path, name, type}`：给图加用户参数(bool/int/int64/float/double/name/string/vector/rotator),基于 `FInstancedPropertyBag::AddProperty`。
  - `read_pcg_params {graph_path}`：列出所有用户参数(name/type/当前值,反射 ExportText)。
  - `set_pcg_param {graph_path, name, value}`：按名设值(反射 ImportText),缺参数报错并列出可用名。
  - 实测 add(float/int/vector)→read→set(2.5/42/vector)→read 往返一致,重名/坏类型/缺参数均拦截。
- **Blueprint 事件通信补齐(2 工具)— 事件分发器**:填补此前"只能读、不能建/删事件分发器"的核心缺失。
  - `bp_add_event_dispatcher {bp_path, dispatcher_name, params?}`:创建事件分发器(多播委托),可选带类型化签名参数(`params` = `[{name,type}]`,如 `OnHealthChanged(float NewHealth)`);基于引擎 `UBlueprintEditorLibrary::AddEventDispatcher` + `AddEventDispatcherParameter`。**参数失败原子回滚**(不残留半成品分发器)。
  - `bp_remove_event_dispatcher {bp_path, dispatcher_name}`:删分发器 + 其签名图。
  - 实测:建无参/带参分发器、重名拦截、坏类型拦截并回滚(delegates 计数正确)、删除、缺失拦截、编译零错。绑定/调用节点用现有 `bp_create_node`。
- **Blueprint 变量补强(2 工具)**：
  - `bp_rename_variable {bp_path, var_name, new_name}`：`FBlueprintEditorUtils::RenameMemberVariable` 改名并**自动 fixup 所有图引用** + 重编译;拦截改名到已存在名 / 源变量不存在。
  - `bp_set_variable_flags {bp_path, var_name, instance_editable?, blueprint_read_only?, expose_on_spawn?, category?, tooltip?}`：改**已有变量**的编辑器面板标志/元数据(instance editable / 只读 / 暴露到 SpawnActor 引脚 / 类别 / tooltip),只改传入的字段。填补了"变量创建后不能改元数据"的缺口。
- **PCG 补强(2 工具)— 生成结果断言 + 图组合**：
  - `pcg_get_stats {actor}`：读某 PCG Volume 生成后**实际产出**——`is_generating`(Generate 异步,轮询至 false)、linked graph、输出数据(total_points + 逐项 pin/type/num_points)、spawned 托管资源(spawned_actors / spawned_components / spawned_instances 来自 ISM/HISM)。这是 PCG 自动化测试的**断言工具**。实测正确读出输出数据类型 `PCGPointArrayData` + 点数,坏 actor 报错。
  - `add_pcg_subgraph_node {graph_path, subgraph_path}`:加 `UPCGSubgraphSettings` 节点执行另一张 PCG 图(组合/复用),`SetSubgraph` 绑定,拦截自引用成环;返回镜像自子图 I/O 的输入输出引脚。
- **新域 Insights(4 工具)— Unreal Insights trace 采集 + 离线分析**：
  - `insights_start_trace {path?, channels?}`/`insights_stop_trace`/`insights_trace_status`：用 `FTraceAuxiliary` 录 `.utrace`(默认 channels `default,stat,counter`,默认落 Saved/Profiling)。
  - `analyze_trace {path, hitch_ms?, counters?}`：`TraceServices::IAnalysisService::Analyze` 离线解析,输出 session 时长 + Game/Rendering 线程帧时分布(count、avg/min/max/median/p95/p99 ms、avg FPS、hitch_count)+ 可选 stat counters(min/max/avg/last)。实测 79s/67MB trace → 游戏线程 1721 帧 avg 8.34ms(119.9fps)。Top CPU timers(瓶颈拆解)后续补。
  - Build.cs 加 `TraceServices` 依赖;修复未闭合帧 EndTime 哨兵值导致 `inf`(非法 JSON)的问题(帧时 >60s 视为无效丢弃)。
- **视觉/坏资产自动化(2 工具)**：
  - **`capture_asset_thumbnail`(Asset)**：用引擎缩略图渲染器把任意 /Game 资产(材质/纹理/网格/蓝图/粒子…)渲成 PNG,**无需打开编辑器**;适中方形尺寸(默认 256,夹取 64–1024,适配 AI 视觉解析)。对材质/纹理编辑做视觉回归最直接。
  - **`find_broken_assets`(Analysis)**:文件夹/全库扫描坏资产——(1)缺失硬引用(依赖的 /Game 包已不存在的悬空引用)、(2)redirector;基于 Asset Registry 图**免加载**,`load_check=true` 额外加载以捕获加载失败。返回坏资产清单 + 逐项原因 + 具体缺失包。实测 /Game 447 资产查出 14 坏(5 redirector + 9 缺失引用)。
- **传输层:渲染类工具的 FAppTime ensure 根治**:命令派发由 `AsyncTask(GameThread)`(TaskGraph 任务,从 worker 派生 → 无继承时间上下文)改为 **CoreTicker(`FTSTicker`)在帧 tick 内直接执行**,使渲染/流式子任务继承有效时间上下文。消除 `capture_asset_thumbnail`/`compile_material`/`take_viewport_screenshot` 触发的 `IsInGameThread() FAppTime` 非致命 ensure(实测 4 缩略图+3 编译+1 视口截图后日志零 ensure)。
- **图域 CRUD 对齐（删除/断连）**：
  - **Material（22）**：`disconnect_material_pins`（断开目标输入引脚,含材质输出 -1）、`remove_material_expression`（按索引删表达式,并清理其它节点/材质输出上指向它的悬空连线,返回 `cleared_references`）。
  - **PCG（11）**：`disconnect_pcg_nodes`（`RemoveEdge`,无边时报错)、`remove_pcg_node`（`RemoveNode` 连带删边,护 input/output 节点）。
  - **Material Function（24 含 Material）**：`disconnect_mf_pins`（断开目标输入引脚)、`remove_mf_expression`（按索引删表达式 + 清悬空连线,返回 `cleared_references`)。
  - 均自描述索引删除后位移；删除后跑 `compile_material` / `read_pcg_graph` 复核。
- **节点防重叠**：Material/MaterialFunction 表达式、PCG 节点、Blueprint 节点无显式坐标时自动错开；`bp_describe_graph` 加 `pos_x/pos_y`，`get_material_info` 加连线 `edges`；`auto_layout_graph` 新增 PCGGraph 分支。
- **5.8 Substrate**：可创建 `UMaterialExpressionSubstrate*` 节点；`connect_material_pins` `dest_input_index=8` → `FrontMaterial`（项目 `r.Substrate=1` 时生效）。
- **Niagara**：`niagara_add_emitter` 修复 5.8 模板路径（CompletelyEmpty/Fountain 回退）。
- **DataTable**：`data_add_row` 修复整表覆盖 bug（改为追加）；去掉逐行自动保存（避免流送 ensure，更快）。
- **level_save**：新增可选 `level_path`，用 `UEditorLoadingAndSavingUtils::SaveMap` 免弹框另存。
- **regen-tools.mjs**：端口发现改为宿主工程无关（支持 `SMITHUE_PROJECT`/`SMITHUE_PORT`）。
- 宿主 target 构建设置对齐 5.8（`BuildSettingsVersion.Latest` + `IncludeOrderVersion.Latest`）。

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
