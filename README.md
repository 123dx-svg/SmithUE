<p align="center">
  <img src="Resources/Icon.png" alt="SmithUE" width="180">
</p>

# SmithUE

<p align="center">
  <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>

<p align="center">
  <a href="README.md">简体中文</a> | <a href="README.en.md">English</a>
</p>

一款高性能虚幻引擎编辑器插件，通过命令协议公开编辑器的全部功能，将重复的手动操作转变为可脚本化、AI 驱动的工作流。

---

## For AI Agents

### 快速部署（Windows）
```bash
# 确认编辑器已启动并启用 SmithUE 插件
npx smithue-cli status
npx smithue-cli exec ping '{}'
npx smithue-cli list
```

### 故障树
1. 编辑器未运行 → 启动虚幻引擎编辑器并等待完全加载
2. 多实例 → 使用 `npx smithue-cli status --pid <pid>` 指定实例
3. 端口文件已过期 → 运行 `npx smithue-cli prune` 清理

---

## 目录

1. [快速入门](#快速入门)
2. [架构](#架构)
3. [命令参考](#命令参考)
4. [连接状态指示器](#连接状态指示器)
5. [AI 纹理生成](#ai-纹理生成)
6. [更新日志](#更新日志)
7. [路线图](#路线图)
8. [已知限制](#已知限制)
9. [贡献](#贡献)
10. [许可证](#许可证)

---

## 快速入门

1. 将仓库克隆到项目的 `Plugins/` 目录，并切换到 `UE5.8` 分支：
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.8 https://github.com/123dx-svg/SmithUE.git
   ```

2. 使用虚幻引擎 5.8 构建项目（首次编译约 50 秒）。

3. 启动编辑器。SmithUE 将自动启动 HTTP 服务器并分配动态端口。

4. 验证安装：
   ```bash
   npx smithue-cli status
   ```

### 安装成功的标志

| 检查项 | 期望结果 |
|--------|---------|
| 编辑器右下角出现 🟢 绿色圆点 + "SmithUE" 文字 | 插件已加载且 HTTP 服务器就绪 |
| `npx smithue-cli status` 返回 `"ready": true` | CLI 能连接到编辑器 |
| `npx smithue-cli exec ping '{}'` 返回 `"message": "pong"` | 命令通道畅通 |

> 如果圆点为灰色或 CLI 报错 `No SmithUE portfiles found`，请等待编辑器完全加载后重试。

### 环境自检与 CLI 自助安装（推荐）

插件启动后会在后台自检 **Node / npm / smithue-cli** 环境（不阻塞编辑器，每步写入 `LogSmithUE` 日志）。打开 **编辑器 → 项目设置 → 插件 → SmithUE → "Status & Updates"** 面板即可：

- 查看 Node / npm / smithue-cli 的版本与状态；
- 一键**安装 / 升级 smithue-cli**：按钮随状态自适应（未安装 → 安装 / 旧版 → 升级 / 已最新 → 灰显确认）。网络不佳时**自动 120s 超时、可随时取消**，失败给出分类提示与手动兜底命令；
- 查看插件更新提醒并跳转 GitHub Releases。

> 即使没装 / 装不上 CLI，**插件本体的 HTTP 工具能力不受影响**——CLI 只是更顺手的消费端客户端。这对在受限网络的企业内网、旧机器上部署尤为友好。

### 多项目共享（可选）

如果需要在多个项目中使用同一份插件代码，使用符号链接：
```bash
# Windows (以管理员运行)
mklink /D "{OtherProject}\Plugins\SmithUE" "{SourceProject}\Plugins\SmithUE"
```
首次打开目标项目时会自动编译插件。

**注意**：SmithUE 现在使用 `smithue-cli` 进行交互。详情请访问：https://github.com/123dx-svg/smithue-cli

---

SmithUE 不只是"操作蓝图的工具"，而是**企业级资产装配与合规标准化引擎**：插件提供通用原子 HTTP 原语，smithue-cli 承载业务规范层（spec 驱动的工厂与合规 linter），规范以 git 可追踪文本存于宿主工程，AI agent 说人话即可批量生成合规蓝图并审计合规性。

---

## 架构

```
AI 工具 (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ smithue-cli (npx smithue-cli exec/list/search/status)
SmithUE UE5 插件 (HTTP :动态端口)
     ↕ UE 反射 API
虚幻引擎 5.8 编辑器
```

---

## 命令参考

SmithUE 提供了分布在 **29 个功能域** 中的 **314 个工具**。命令集正在持续扩展。请使用 `npx smithue-cli list` 查看最新可用命令，或参阅 [TOOLS.md](TOOLS.md) 获取完整参考。

### 功能域概览

| 功能域 | 工具数 | 描述 |
|---|---|---|
| Blueprint | 43 | 蓝图创建、节点、函数、变量、组件、DSL 编译器、健康检查、diff、AnimGraph 编辑 |
| Material | 20 | 材质、材质实例、MPC、材质函数 |
| Niagara | 17 | 粒子系统创建、发射器、模块、渲染器 |
| Analysis | 13 | 源码分析、依赖图、蓝图诊断、资产校验 |
| Asset | 18 | 资产增删改查、浏览器操作、内容浏览器选择/导航、AI 纹理生成 |
| Level | 12 | 关卡管理、地形、植被 |
| PIE | 11 | 运行模式：启动/停止、Actor、属性、控制台 |
| Environment | 11 | 后处理、雾、天空、光照、物理、样条 |
| Editor | 10 | Actor 生成、属性、后处理、项目设置 |
| Observation | 8 | 面板、编辑器状态、Actor 属性、世界大纲 |
| Animation | 7 | 动画蒙太奇、动画蓝图、段落、通知 |
| Interaction | 7 | 控制台/编辑器命令、撤销/重做、按键模拟 |
| Sequencer | 6 | 关卡序列创建、绑定、轨道、关键帧 |
| Viewport | 6 | 摄像机控制、截图、Actor 选择 |
| Input | 6 | 增强输入：InputAction、InputMappingContext |
| Data | 8 | 数据表、用户定义结构体、用户定义枚举 |
| System | 5 | 服务器连接、会话管理 |
| Project | 4 | 项目信息、插件、目录、源文件 |
| UMG | 4 | 控件蓝图创建、控件树、属性 |
| Debug | 3 | 蓝图断点：设置、清除、列出 |
| Curve | 4 | 曲线资产(Float/LinearColor/Vector) + 颜色图集 |
| RenderTarget | 2 | 纹理渲染目标 |
| Physics | 2 | 物理材质(摩擦/弹性/密度) |
| LiveCoding | 2 | Live Coding 热编译：支持状态查询与同步触发编译 |

---

## 连接状态指示器

SmithUE 在编辑器状态栏显示一个圆形指示器，实时反映连接状态。

### 指示器颜色

| 颜色 | 状态 |
|---|---|
| 🟢 绿色 | 就绪 — HTTP 服务器运行中，可通过 CLI 连接 |
| 🟡 黄色 | 就绪但 PIE 运行中 — 部分写命令被锁定 |
| ⚫ 灰色 | 未就绪 — 服务器未启动或正在初始化 |

### 交互功能

- **Tooltip 悬停**：显示项目名称、端口号、PIE 状态
- **点击圆点**：复制端口号到剪贴板

---

## AI 纹理生成

`generate_texture` 命令将编辑器与现代生成式 AI 连接。它根据提供的端点 URL 自动检测 API 格式，支持 DALL-E、Imagen 和基于 OpenAI Chat 的生成。

**使用示例：**
```bash
npx smithue-cli exec generate_texture '{"params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## 更新日志

> 完整历史见 [CHANGELOG.md](CHANGELOG.md)。

### v1.16.2（UE5.8，2026-07-28）
- **修复 SKILL 部署漏文件**：面板「重装 SKILL」与 CLI 部署此前只拷 `SKILL.md`，漏掉 `reference/` 与 `scripts/`。`ReinstallSkill()` 改为递归拷整个 bundle；`ComputeSkillState()` 检测到缺 reference/scripts 即判 Stale（重装按钮出现），存量单文件机器可自愈。配合 smithue-cli 0.14.1。

### v1.16.1（UE5.8，2026-07-28）
- 同步已发布 smithue-cli **0.14.0**：`kRecommendedCliVersion` → 0.14.0（旧 CLI 在面板提示升级、SKILL 漂移自愈）。
- 文档：PITFALLS #7（消费端 `--params-file` / `smithue.ps1` 传参、CJK）、#12（CI 自动发布、bypass-2fa 废弃 → OIDC）；测试报告加 CLI 0.14.0 脚注。仅常量 + 文档，无工具变更。

### v1.16.0（UE5.8，2026-07-22）
- **升级到 Unreal Engine 5.8**：API 兼容适配（材质表达式 `CountInputs`/`GetMaterialResource(EShaderPlatform)`、`StructUtils/UserDefinedStruct.h`、JSON key `TSharedString`、AnimGraph 绑定反射、`EAutomationTestFlags` scoped、`.uplugin` `PlatformAllowList` 等），弃用告警清零。详见 [docs/UE5.8-UPGRADE-TEST-REPORT.md](docs/UE5.8-UPGRADE-TEST-REPORT.md)。
- **新增 AI 域（10 工具）**：黑板（建/加键/读）、行为树（建/链黑板/读）、EQS（建/读）、状态树（建+ComponentSchema+编译/读）。
- **新增 PCG 域（7 工具）**：PCG 图创建 + 图内节点/连线（`add_pcg_node`/`connect_pcg_nodes`）+ 生成 + 关卡 Volume。
- **新增 Dialog 域（3 工具，worker-safe）**：识别并响应阻塞式编辑器模态框（如 `level_save` 的"另存为"），模态卡死 game thread 期间仍可用。
- **蓝图接口**：`create_blueprint_interface` + `bp_implement_interface`。
- **节点防重叠 + 自动布局**：Material/MaterialFunction/PCG/Blueprint 新建节点自动错开；`auto_layout_graph` 新增 PCG 分支；`get_material_info` 返回连线 `edges`。
- **5.8 新材质系统 Substrate**：可创建 `SubstrateSlabBSDF` 等节点，`connect_material_pins` 支持 `dest_input_index=8`（FrontMaterial）。
- **修复**：Niagara emitter 模板路径（5.8）、DataTable 多行追加、`level_save` 支持 `level_path` 免弹框另存。
- 共 **314 工具 / 29 域**。

### v1.13.0（UE5.2，2026-06-26）
- **SKILL 漂移检测**：每次环境探针后对比本地已部署 `smithue-control/SKILL.md` 与已安装 CLI 自带 bundle，结果写入 `LogSmithUE`（Unknown / NotDeployed / Stale / Synced）。
- **「Status & Updates」面板新增 SKILL 状态行 + 「重装 SKILL」按钮**：漂移时一键将 CLI bundle 复制到全部 agent 技能目录，重装后自动重探针。
- **`kRecommendedCliVersion` 升至 `0.13.4`**：旧 CLI 机器自动进入 *Outdated → 升级* 流程，升级触发 postinstall 重新部署最新 SKILL，构成漂移自愈闭环。

### v1.12.0（UE5.2，2026-06-26）
- **材质工具自描述**：`connect_material_pins` 的 `dest_input_index` 补全 `7=WorldPositionOffset`（此前只列 0–6）；`set_expression_property` 描述列出按节点类型的合法键，失败错误改为**回显该节点合法键**。沉淀 PITFALLS #15。

### v1.11.0（UE5.2，2026-06-25）
- **启动环境自检**：编辑器启动后后台检测 Node / npm / smithue-cli 环境，不阻塞主线程，每步写入 `LogSmithUE` 日志。
- **「Status & Updates」设置面板**：查看环境状态、插件更新提醒（GitHub Releases 链接），一键安装 / 升级 CLI；按钮随状态自适应（未安装 / 升级 / 已最新 / 取消）。
- **有界安装**：CLI 安装走 **120s 硬超时 + 可取消 + npm 快速失败参数**，网络不佳时不假死；失败给出分类提示（权限 / 网络 / 超时）与手动兜底。

### v1.10.0
- **修复 `level_new` 崩溃**：建图延迟到下一帧安全点，避免在 HTTP handler 内销毁世界导致崩溃。
- **新增** `spawn_mesh_actor`（带网格 + 材质）、`level_add_basic_env`（一键基础光照 / 天空 / 雾 / 地板）。
- **新增 LiveCoding 域**（`livecoding_status` / `livecoding_compile`）。
- 工具总数达 **221 / 24 域**。

### v0.8.0，CLI 迁移
- 移除 TCP Server / ConnectionManager / SessionManager；HTTP 改动态端口 + 端口文件发现。
- `/ready` 端点 + 启动期 503 守卫；StatusIndicator 重写为 CLI-aware 小圆点。
- 新 `smithue-cli` npm 包取代 MCP。

> 更早的版本（v1.x 蓝图深度、N-id 会话、度量、合规 linter 等）详见 [CHANGELOG.md](CHANGELOG.md)。

---

## 路线图

> **定位：与市面上其他 UE AI 插件不同。** 多数 UE AI 插件停留在"让 AI 操作编辑器"的演示场景；SmithUE 面向企业真实痛点——**旧项目协同**与**资产合规标准化**，并为**未来的转码 / 迁移规范化**做基础设施铺垫。它交付的不是一次性自动化脚本，而是一套 **git 可追踪、可审计、可团队复用的"原子工具层 + 规范层"**。

### 第一阶段（已落地）— 原子能力底座
- 24 个功能域、229 个原子 HTTP 工具，覆盖蓝图 / AnimGraph / 材质 / Niagara / 关卡 / 资产 / 分析等。
- spec 驱动的蓝图工厂 + 合规 linter：规范以 git 文本存于宿主工程，AI 说人话即可批量生成合规蓝图并审计。
- 插件环境自检与自助部署（启动检测 Node/npm/CLI、设置面板一键安装升级），降低旧机器 / 旧项目 / 内网环境的接入门槛。

### 第二阶段 — 旧项目协同与合规标准化
- **旧资产批量扫描与审计**：命名 / 目录 / 父类 / 材质槽 / LOD / 碰撞等规则化体检，输出可追踪的合规报告。
- **批量标准化修复**：按团队 spec 一键规整存量资产，差异可审阅、可回滚。
- **团队规范协同**：spec 文件随工程入库，多人共享同一套合规基线；在 CI 中运行合规 linter 作为门禁。

### 第三阶段 — 转码 / 迁移规范化铺垫
- **迁移规则引擎**：把"旧形态 → 标准形态"的转换沉淀为可声明、可复用的规则集。
- **跨版本 / 跨格式资产转码流水线**：为引擎升级、资产格式迁移、规范化重构提供可重复执行的管道。
- **全链路审计**：每一步转码 / 标准化操作留痕，满足企业合规与回溯要求。

### 第四阶段 — 规模化协同
- 多客户端并发协作模式。
- 批量 / 事务化执行（单请求多命令、原子回滚）。
- 仪表盘与报表：资产健康度、合规覆盖率、迁移进度可视化。
- Python SDK / REST OpenAPI / WebSocket 实时事件流。

---

## 已知限制

- **并发性**：仅支持单客户端连接。
- **输入焦点**：`simulate_key` 需要视口处于焦点状态。
- **属性类型**：`set_actor_property` 不支持 `TMap` 或委托等高级类型。嵌套结构体属性需使用专用命令。
- **场景纹理**：`SceneTextureId` 通过 FProperty 反射设置，以避免未导出引擎符号的链接错误。
- **操作系统**：仅限 Windows Win64。
- **CLI 为可选消费端**：`smithue-cli` 是便利客户端，其安装 / 升级依赖网络（已内置 120s 超时、可取消与手动兜底）；**插件本体不依赖它即可工作**。
- **引擎版本**：当前主线针对 UE 5.2；其他版本需自行适配。

---

## 贡献

请参阅 [CONTRIBUTING.md](CONTRIBUTING.md) 获取添加新命令的详细步骤。

### AI 辅助开发

AI 编码助手（OpenCode、Claude Code、Cline 等）应读取仓库根目录的以下文件：

- **[AGENTS.md](AGENTS.md)** — 仓库边界、构建命令、测试、高频踩坑
- **[docs/spec/](docs/spec/)** — 工具开发规范（TOOL_SPEC + NAMING + PITFALLS）

运行时操作编辑器（非开发）的 `smithue-control` 技能随 [smithue-cli](https://www.npmjs.com/package/smithue-cli) 发布（`smithue-cli skill --install`）。

---

## 历史：MCP 服务已停用
MCP 服务已在 v0.8.0 中移除，由 `smithue-cli` 替代。迁移指南请参见 [smithue-cli MIGRATION.md](https://github.com/123dx-svg/smithue-cli/blob/main/MIGRATION.md)。

---

## 卸载

完全卸载前，建议先运行 `smithue-cli purge` 清理 `%LOCALAPPDATA%\.smithue\` 目录中的残留文件。

```bash
# 预览将被删除的内容（不实际删除）
smithue-cli purge --dry-run

# 非交互式完整清理（适用于脚本/CI）
smithue-cli purge -y
```

`purge` 命令会在删除前进行存活检测，并在目录为符号链接时拒绝操作以保障安全。完整参数说明请参阅 [smithue-cli README — Uninstall](https://github.com/123dx-svg/smithue-cli#uninstall)。

清理完成后，再执行：
```bash
npm uninstall -g smithue-cli
```

---

## 许可证

Copyright 2026, 123dx-svg. MIT License.
