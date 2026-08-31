# SmithUE

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

还没来得及做完！！！别用

---

## Table of Contents / 目录

1. [Vision / 愿景](#vision--愿景)
2. [Quick Start / 快速入门](#quick-start--快速入门)
3. [MCP Server / MCP 服务](#mcp-server--mcp-服务)
4. [Protocol / 协议](#protocol--协议)
5. [Command Reference / 命令参考](#command-reference--命令参考)
6. [AI Texture Generation / AI 纹理生成](#ai-texture-generation--ai-纹理生成)
7. [Roadmap / 路线图](#roadmap--路线图)
8. [Changelog / 更新日志](#changelog--更新日志)
9. [Known Limitations / 已知限制](#known-limitations--已知限制)
10. [Contributing / 贡献](#contributing--贡献)
11. [License / 许可证](#license--许可证)

---

## Vision / 愿景

Unreal Engine developers spend significant time on repetitive manual operations: searching for assets, dragging actors into levels, configuring material parameters, and manually connecting Blueprint nodes. SmithUE transforms the editor into a programmable environment. Imagine generating a complete level layout from a text description, automatically analyzing complex module dependencies, or creating sophisticated Blueprint logic via external scripts. SmithUE frees you from the friction of the UI, allowing you to focus on high-level design and architectural decisions.

虚幻引擎开发人员在重复的手动操作上花费了大量时间: 搜索资产, 将 Actor 拖入关卡, 配置材质参数以及手动连接蓝图节点. SmithUE 将编辑器转变为可编程环境. 想象一下根据文本描述生成完整的关卡布局, 自动分析复杂的模块依赖关系, 或通过外部脚本创建复杂的蓝图逻辑. SmithUE 将您从 UI 的摩擦中解放出来, 允许您专注于高级设计和架构决策.

---

## Quick Start / 快速入门

1. Clone the repository into your UE project's `Plugins/` directory:
   将仓库克隆到 UE 项目的 `Plugins/` 目录：
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.7 https://github.com/123dx-svg/SmithUE.git
   ```

2. Build your project using Unreal Engine 5.7.
   使用虚幻引擎 5.7 构建项目.

3. Launch the Unreal Editor. SmithUE will automatically start the HTTP server on port 13721.
   启动虚幻编辑器. SmithUE 将在 HTTP 端口 13721 上自动初始化命令服务器.

4. Verify the connection:
   验证连接：
   ```bash
   curl -X POST http://localhost:13721/api/v1/execute -H "Content-Type: application/json" -d "{\"command\":\"ping\"}"
   ```

5. Configure AI tool (see [AI Tool Configuration](#ai-tool-configuration)).
   配置 AI 工具（参见 [AI 工具配置](#ai-tool-configuration)）。

> **Note / 注意**: The MCP Server is pre-built as `Scripts/SmithUE-MCP/dist/bundle.js`. No `npm install` or `npm run build` required for end users. Only Node.js 18+ runtime is needed.
> MCP 服务已预构建为 `Scripts/SmithUE-MCP/dist/bundle.js`。终端用户无需执行 `npm install` 或 `npm run build`，只需安装 Node.js 18+ 运行时。

---

## MCP Server / MCP 服务

SmithUE includes a TypeScript MCP (Model Context Protocol) Server that bridges AI tools to the UE5 plugin. It uses a **meta-tool architecture** — 3 fixed tools instead of registering every command individually, keeping AI context usage constant (~300 tokens) regardless of command count.

SmithUE 内置 TypeScript MCP 服务，通过 **元工具架构** 桥接 AI 工具与 UE5 插件。仅注册 3 个固定工具，无论命令数量多少，AI 上下文占用恒定（约 300 tokens）。

```
AI Tool (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ MCP stdio
SmithUE MCP Server (Scripts/SmithUE-MCP/)
     ↕ HTTP :13721
SmithUE UE5 Plugin
     ↕ UE Reflection API
Unreal Engine 5.7 Editor
```

### Context Impact / 上下文影响

| Approach / 方案 | N tools | 200 tools | 1000 tools |
|---|---|---|---|
| Full registration / 全量注册 | ~6.5K tokens | ~20K tokens | ~100K tokens |
| Meta-tool (SmithUE) / 元工具 | ~300 tokens | ~300 tokens | ~300 tokens |

### 3 Meta-Tools / 3 个元工具

| Tool | Description / 描述 |
|---|---|
| `smithue_list_domain` | List domains or get full command schemas for a domain / 列出域或获取某域的完整命令模式 |
| `smithue_search` | Search commands by keyword / 按关键词搜索命令 |
| `smithue_execute` | Execute any command with parameters / 执行任意命令 |

### Install & Run / 安装与运行

The MCP Server is pre-built and ready to use. No build step required.
MCP 服务已预构建，开箱即用，无需构建步骤。

Start the MCP Server (requires UE Editor running with SmithUE plugin):
启动 MCP 服务（需要 UE 编辑器运行并启用 SmithUE 插件）：

```bash
node Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

Environment variables / 环境变量：
- `SMITHUE_PORT` — HTTP port (default: `13721`)
- `SMITHUE_HOST` — Host address (default: `localhost`)
- `SMITHUE_CLIENT_NAME` — Client display name for connection indicator (default: `OpenCode`)

### AI Tool Configuration / AI 工具配置

#### OpenCode

Add to `opencode.json` in your project root:
在项目根目录的 `opencode.json` 中添加：

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "smithue": {
      "type": "local",
      "command": ["node", "{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

> **Note / 注意**: OpenCode does NOT use `mcp.json`. Configuration must be in `opencode.json`.
> OpenCode 不使用 `mcp.json`，必须配置在 `opencode.json` 中。

#### Claude Code

```bash
claude mcp add smithue -- node {YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js serve
```

#### GitHub Copilot

Create or update `.github/copilot-mcp.json`:
创建或更新 `.github/copilot-mcp.json`：

```json
{
  "mcpServers": {
    "smithue": {
      "command": "node",
      "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
    }
  }
}
```

#### Cline

Add to VSCode settings under `cline.mcpServers`:
在 VSCode 设置中添加 `cline.mcpServers`：

```json
{
  "smithue": {
    "command": "node",
    "args": ["{YourProject}/Plugins/SmithUE/Scripts/SmithUE-MCP/dist/bundle.js", "serve"]
  }
}
```

### Workflow / 使用流程

```
1. smithue_list_domain()          → See all 20 domains / 查看全部 20 个域
2. smithue_list_domain("Material") → Get Material command schemas / 获取材质命令模式
3. smithue_search("blueprint")     → Find blueprint-related commands / 搜索蓝图相关命令
4. smithue_execute("create_material", {"name": "M_Test", "path": "/Game/Materials"})
                                   → Execute command / 执行命令
```

---

### Connection Status Indicator / 连接状态指示器

SmithUE displays a circular status indicator in the editor's bottom-right status bar:
SmithUE 在编辑器右下角状态栏显示圆形连接状态指示器：

| Color / 颜色 | State / 状态 |
|---|---|
| 🟢 Green / 绿色 | Connected — MCP client session active / 已连接 — MCP 客户端会话活跃 |
| 🔴 Red / 红色 | Disconnected — no active sessions / 未连接 — 无活跃会话 |
| 🟡 Red↔Yellow blink / 红黄闪烁 | State change — session connecting or disconnecting / 状态变更中 |

Hover over the indicator to see client name, connection count, and available tool count.
悬停查看客户端名称、连接数和可用工具数。

## Protocol / 协议

SmithUE uses a standard JSON format for all requests and responses, supporting both high-performance raw TCP sockets and standard HTTP requests.

SmithUE 在所有请求和响应中使用标准 JSON 格式, 同时支持高性能原始 TCP 套接字和标准 HTTP 请求.

**Request Format / 请求格式:**
```json
{
  "command": "command_name",
  "params": {
    "param_key": "param_value"
  }
}
```

**Response Format / 响应格式:**
```json
{
  "status": "success",
  "data": { ... }
}
```

---

## Command Reference / 命令参考

SmithUE provides **180 tools** organized across **20 functional domains**. The command set is continuously growing — see `smithue_list_domain()` for the latest available commands, or refer to [TOOLS.md](TOOLS.md) for the full reference.

SmithUE 提供了分布在 **20 个功能领域** 的 **180 个专业工具**. 命令集持续增长中——使用 `smithue_list_domain()` 查看最新可用命令，或参阅 [TOOLS.md](TOOLS.md) 获取完整参考.

### Domain Overview / 领域概览

| Domain / 领域 | Tools / 工具数 | Description / 描述 |
|---|---|---|
| System / 系统 | 5 | Server connectivity, session management / 服务器连接、会话管理 |
| Project / 项目 | 4 | Project info, plugins, folders, source files / 项目信息、插件、目录、源文件 |
| Material / 材质 | 20 | Materials, material instances, MPC, material functions / 材质、材质实例、MPC、材质函数 |
| Asset / 资产 | 12 | Asset CRUD, browser operations, AI texture generation / 资产增删改查、浏览器操作、AI 纹理生成 |
| Editor / 编辑器 | 8 | Actor spawning, properties, post-process, project settings / Actor 生成、属性、后处理、项目设置 |
| Interaction / 交互 | 7 | Console/editor commands, undo/redo, key simulation / 控制台/编辑器命令、撤销/重做、按键模拟 |
| Blueprint / 蓝图 | 15 | BP creation, nodes, functions, variables, components, DSL compiler / 蓝图创建、节点、函数、变量、组件、DSL 编译器 |
| Viewport / 视口 | 6 | Camera control, screenshots, actor selection / 摄像机控制、截图、Actor 选择 |
| Observation / 观测 | 7 | Panels, editor state, actor properties, world outline / 面板、编辑器状态、Actor 属性、世界大纲 |
| Analysis / 分析 | 13 | Source analysis, dependency graphs, BP diagnostics, asset validation / 源码分析、依赖图、蓝图诊断、资产校验 |
| Niagara / 粒子 | 17 | Particle system creation, emitters, modules, renderers / 粒子系统创建、发射器、模块、渲染器 |
| Level / 关卡 | 11 | Level management, landscape, foliage / 关卡管理、地形、植被 |
| Data / 数据 | 6 | DataTables, UserDefinedStructs, UserDefinedEnums / 数据表、用户定义结构体、用户定义枚举 |
| Sequencer / 序列 | 6 | LevelSequence creation, bindings, tracks, keyframes / 关卡序列创建、绑定、轨道、关键帧 |
| Environment / 环境 | 11 | Post-process, fog, sky, lights, physics, splines / 后处理、雾、天空、光照、物理、样条 |
| PIE / 运行 | 11 | Play-In-Editor: start/stop, actors, properties, console / 运行模式：启动/停止、Actor、属性、控制台 |
| Animation / 动画 | 7 | AnimMontage, AnimBlueprint, sections, notifies / 动画蒙太奇、动画蓝图、段落、通知 |
| Input / 输入 | 6 | Enhanced Input: InputAction, InputMappingContext / 增强输入：输入动作、输入映射上下文 |
| PCG / 程序化内容生成 | 4 | PCG graph creation, volume spawning, generation / PCG图创建、体积生成、触发生成 |
| UMG / 控件蓝图 | 4 | Widget blueprint creation, tree reading, widget management / 控件蓝图创建、树读取、控件管理 |

> 📖 For detailed parameter schemas and usage of each tool, see **[TOOLS.md](TOOLS.md)**.
> 📖 每个工具的详细参数模式和用法，请参阅 **[TOOLS.md](TOOLS.md)**.

---

## AI Texture Generation / AI 纹理生成

The `generate_texture` command bridges the editor with modern generative AI. It automatically detects the target API format based on the provided endpoint URL, supporting DALL-E, Imagen, and OpenAI Chat-based generation.

`generate_texture` 命令将编辑器与现代生成式 AI 连接起来. 它根据提供的端点 URL 自动检测目标 API 格式, 支持 DALL-E, Imagen 和基于 OpenAI Chat 的生成.

**Example Usage / 使用示例:**
```bash
curl -X POST http://localhost:13721/api/v1/execute \
  -H "Content-Type: application/json" \
  -d '{"command":"generate_texture","params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## Roadmap / 路线图

We are committed to expanding SmithUE into a comprehensive AI-first development toolkit, with a focus on digital twin and simulation workflows.

我们致力于将 SmithUE 扩展为全面的 AI 优先开发工具包, 聚焦数字孪生与仿真工作流.

*   **v1.1**
    *   MCP (Model Context Protocol) support for direct LLM integration.
        支持 MCP (模型上下文协议), 实现与大语言模型的直接集成.
    *   Blueprint logic generation - AI-driven EventGraph / FunctionGraph construction from natural language.
        蓝图逻辑生成 - 从自然语言驱动 AI 自动构建事件图 / 函数图.
        🔧 **Progress in v1.0.2** — `bp_set_component_property` (SCS/inherited component template properties, PostProcessMaterial special handling), `bp_create_node` now supports `K2Node_InputKey` for keyboard event binding. Verified with thermal vision toggle (PostProcessComponent + InputKey T + FlipFlop).
        🔧 **v1.0.2 进展** — `bp_set_component_property` (SCS/继承组件模板属性, PostProcessMaterial 特殊处理), `bp_create_node` 现支持 `K2Node_InputKey` 键盘事件绑定. 已通过热成像切换验证 (PostProcessComponent + InputKey T + FlipFlop).
    *   ~~Material Blueprint generation - programmatic material node network assembly.~~
        ~~材质蓝图生成 - 程序化材质节点网络组装.~~
        ✅ **Done in v1.0.1** — Full material creation pipeline: `create_material` → `set_material_property` → `add_material_expression` → `set_expression_property` → `connect_material_pins` → `compile_material` → `add_postprocess_material`. Verified with a thermal vision post-process material end-to-end.
        ✅ **已在 v1.0.1 完成** — 完整材质创建流水线: `create_material` → `set_material_property` → `add_material_expression` → `set_expression_property` → `connect_material_pins` → `compile_material` → `add_postprocess_material`. 已通过热成像后处理材质端到端验证.
        ✅ **Done in v1.0.3** — MaterialFunction pipeline (`create_material_function` → `add_mf_expression` → `set_mf_expression_property` → `connect_mf_pins`), Material Editor live refresh (operations reflect in open editor without restart), asset management (`delete_asset`, `move_asset`, `asset_editor` for batch open/close), `compile_material` enhanced with shader-level and node-level error reporting.
        ✅ **已在 v1.0.3 完成** — 材质函数流水线 (`create_material_function` → `add_mf_expression` → `set_mf_expression_property` → `connect_mf_pins`), 材质编辑器实时刷新 (操作实时反映在已打开的编辑器中), 资产管理 (`delete_asset`, `move_asset`, `asset_editor` 批量打开/关闭), `compile_material` 增强着色器级和节点级错误报告.
*   **v1.2**
    *   Digital twin scene construction - procedural environment generation from data sources (GIS, point cloud, CAD).
        数字孪生场景构建 - 从数据源 (GIS, 点云, CAD) 程序化生成环境.
    *   Sensor simulation commands - LiDAR, camera, radar actor spawning and configuration.
        传感器仿真命令 - LiDAR, 摄像头, 雷达 Actor 的生成与配置.
    *   Python binding / REST SDK.
        Python 绑定 / REST SDK.
*   **v1.3**
    *   ~~Runtime game state observation (PIE mode commands).~~
        ~~运行时游戏状态观察 (PIE 模式命令).~~
        ✅ **Done in v0.2.0** — PIE domain (11 tools): start/stop/pause PIE, query actors & properties, execute console commands in runtime, auto-detect PIE world.
        ✅ **已在 v0.2.0 完成** — PIE 领域 (11 工具): 启停/暂停 PIE, 查询 Actor 及属性, 运行时执行控制台命令, 自动检测 PIE 世界.
    *   Live data streaming - real-time external data injection into simulation actors.
        实时数据流 - 将外部实时数据注入仿真 Actor.
    *   Multi-client collaboration mode.
        多客户端协作模式.
*   **v2.0**
    *   Full AI copilot mode - natural language → Blueprint compilation pipeline.
        全 AI 辅助驾驶模式 - 自然语言 → 蓝图编译流水线.
    *   Scenario orchestration - programmatic traffic / pedestrian / weather control for simulation.
        场景编排 - 程序化交通 / 行人 / 天气控制用于仿真.

---

## Changelog / 更新日志

### v1.3.0 — Blueprint 组件系统增强

**新增功能 / New Features:**
- `bp_add_component` 支持 `parent` 参数，可指定父组件实现层级挂载
  `bp_add_component` supports `parent` param for hierarchical component attachment
- `bp_remove_component` 新命令，从蓝图 SCS 中移除组件（子组件自动上移到父节点）
  New `bp_remove_component` command to remove components from SCS (children reparented automatically)
- `bp_get_summary` 返回组件的 `parent`/`children` 嵌套关系
  `bp_get_summary` now returns component hierarchy with `parent`/`children` fields
- `bp_create` 支持三种父类格式：C++ 类名、蓝图类名（`_C` 后缀）、蓝图资产路径（`/Game/...`）
  `bp_create` supports three parent class formats: C++ class name, BP class name (`_C` suffix), BP asset path (`/Game/...`)
- `bp_create_node` 支持 `Class::Function` 简写自动创建 `K2Node_CallFunction` 节点
  `bp_create_node` supports `Class::Function` shorthand to auto-create CallFunction nodes
- `bp_create_node` 支持 `K2Node_EnhancedInputAction`（通过 `input_action` 参数指定 InputAction 资产）
  `bp_create_node` supports `K2Node_EnhancedInputAction` (via `input_action` extra param)
- `bp_create_node` 支持 `K2Node_DynamicCast`（通过 `target_class` 参数指定目标类）
  `bp_create_node` supports `K2Node_DynamicCast` (via `target_class` extra param)
- 新增 `InputBlueprintNodes` 模块依赖，支持增强输入蓝图节点
  Added `InputBlueprintNodes` module dependency for Enhanced Input Blueprint nodes

**Bug 修复 / Bug Fixes:**
- 修复蓝图组件重编译崩溃：编译时始终使用 `SkipGarbageCollection` 防止 GC 过早销毁被引用的模板对象
  Fixed Blueprint component recompilation crash: always use `SkipGarbageCollection` to prevent GC from prematurely destroying referenced templates
- 修复蓝图派生组件添加到 SCS 时的纯虚函数崩溃：创建模板后禁用 Tick，添加前自动编译未编译的组件蓝图
  Fixed pure virtual crash when adding BP-derived components to SCS: disable tick on template, auto-compile uncompiled component BPs before adding

---

## Known Limitations / 已知限制

*   **Concurrency**: Single-client connection only.
    并发性: 仅支持单客户端连接.
*   **Input Focus**: `simulate_key` requires viewport focus.
    输入焦点: `simulate_key` 需要视口焦点.
*   **Property Types**: Advanced types like `TMap` or delegates are not supported by `set_actor_property`. Nested struct properties (e.g. `Settings.WeightedBlendables`) require dedicated commands.
    属性类型: `set_actor_property` 不支持 `TMap` 或委托等高级类型. 嵌套结构体属性 (如 `Settings.WeightedBlendables`) 需使用专用命令.
*   **SceneTexture**: `SceneTextureId` is set via FProperty reflection to avoid link errors with unexported engine symbols.
    场景纹理: `SceneTextureId` 通过 FProperty 反射设置, 以避免未导出引擎符号的链接错误.
*   **OS**: Windows Win64 only.
    操作系统: 仅限 Windows Win64.

---

## Contributing / 贡献

See [CONTRIBUTING.md](CONTRIBUTING.md) for step-by-step instructions on adding new commands.
参见 [CONTRIBUTING.md](CONTRIBUTING.md) 了解如何添加新命令。

### AI-Assisted Development Skill / AI 辅助开发技能

SmithUE provides an AI development skill file (`Docs/smithue-dev/SKILL.md`) that teaches AI coding assistants how to contribute to this project. After installation, your AI assistant will understand the plugin's architecture, coding conventions, command registration patterns, and Git workflow — enabling it to help you add new commands, fix bugs, and submit code correctly.

SmithUE 提供了一份 AI 开发技能文件（`Docs/smithue-dev/SKILL.md`），它能教会 AI 编码助手如何参与本项目开发。安装后，你的 AI 助手将理解插件架构、代码规范、命令注册模式和 Git 工作流——帮助你添加新命令、修复 Bug 并正确提交代码。

**Install / 安装**:

Copy the skill directory to your AI tool's skill location:
将技能目录复制到 AI 工具的技能路径：

```bash
# OpenCode (project-level / 项目级)
cp -r Plugins/SmithUE/Docs/smithue-dev {YourProject}/.agents/skills/

# OpenCode (user-level / 用户级)
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.agents/skills/

# Claude Code
cp -r Plugins/SmithUE/Docs/smithue-dev ~/.claude/skills/
```

Once installed, the AI assistant will automatically activate this skill when you work on SmithUE-related tasks.
安装后，当你处理 SmithUE 相关任务时，AI 助手会自动激活此技能。

---

## License / 许可证

Copyright 2026, 123dx-svg. MIT License.
版权所有 2026, 123dx-svg. 基于 MIT 许可证发布.
