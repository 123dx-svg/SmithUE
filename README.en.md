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

A high-performance Unreal Engine editor plugin that exposes the editor's full capabilities through a command protocol, turning repetitive manual operations into scriptable, AI-driven workflows.

---

## For AI Agents

### Quick Deployment (Windows)
```bash
# Verify editor is running and SmithUE plugin is enabled
npx smithue-cli status
npx smithue-cli exec ping '{}'
npx smithue-cli list
```

### Troubleshooting Tree
1. Editor not running → Start Unreal Engine Editor and wait for full load
2. Multiple instances → Use `npx smithue-cli status --pid <pid>` to specify instance
3. Stale portfiles → Run `npx smithue-cli prune` to clean up

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture](#architecture)
3. [Command Reference](#command-reference)
4. [Connection Status Indicator](#connection-status-indicator)
5. [AI Texture Generation](#ai-texture-generation)
6. [Changelog](#changelog)
7. [Roadmap](#roadmap)
8. [Known Limitations](#known-limitations)
9. [Contributing](#contributing)
10. [License](#license)

---

## Quick Start

1. Clone into `{YourProject}/Plugins/` on branch `UE5.8`:
   ```bash
   cd {YourProject}/Plugins
   git clone -b UE5.8 https://github.com/123dx-svg/SmithUE.git
   ```

2. Build your project with Unreal Engine 5.8.

3. Launch the editor. SmithUE automatically starts the HTTP server and assigns a dynamic port.

4. Verify the connection:
   ```bash
   npx smithue-cli status
   ```

### Self-check & one-click CLI install (recommended)

On startup the plugin detects your **Node / npm / smithue-cli** environment in the background (non-blocking; each probe is logged to `LogSmithUE`). Open **Editor → Project Settings → Plugins → SmithUE → "Status & Updates"** to:

- See Node / npm / smithue-cli versions and status;
- **Install / upgrade smithue-cli** with one click. The button adapts to state (Not installed → Install / Outdated → Upgrade / Ready → disabled confirmation). On a poor network it **auto-times-out after 120s and is cancellable**, with classified failure hints and a manual fallback;
- See plugin-update reminders with a link to GitHub Releases.

> Even without the CLI installed, **the plugin's HTTP tool capabilities are unaffected** — the CLI is just a convenience consumer client. This is especially friendly for restricted enterprise intranets and legacy machines.

**Note**: SmithUE now uses `smithue-cli` for interaction. Details at: https://github.com/123dx-svg/smithue-cli

---

## Architecture

```
AI Tool (OpenCode / Claude Code / Cline / GitHub Copilot)
     ↕ smithue-cli (npx smithue-cli exec/list/search/status)
SmithUE UE5 Plugin (HTTP :dynamic port)
     ↕ UE Reflection API
Unreal Engine 5.8 Editor
```

---

## Command Reference

SmithUE provides **314 tools** organized across **29 functional domains**. The command set is continuously growing. Use `npx smithue-cli list` to see the latest available commands, or refer to [TOOLS.md](TOOLS.md) for the full reference.

### Domain Overview

| Domain | Tools | Description |
|---|---|---|
| Blueprint | 43 | BP creation, nodes, functions, variables, components, DSL, health/diff/trace, bulk component edit, AnimGraph editing |
| Material | 20 | Materials, material instances, MPC, material functions |
| Niagara | 17 | Particle systems, emitters, modules, renderers, parameters |
| Asset | 18 | Asset CRUD, browser ops, content browser selection/navigation, AI texture generation |
| Analysis | 13 | Source analysis, dependency graphs, BP diagnostics, asset validation |
| Level | 12 | Level management, landscape, foliage |
| Environment | 11 | Post-process, fog, sky, lights, physics, collision, splines |
| PIE | 11 | Play-In-Editor: start/stop, actors, properties, console |
| Editor | 10 | Actor spawning, properties, post-process, project settings |
| Data | 8 | DataTables, structs, enums, data assets |
| Observation | 8 | Panels, editor state, actor properties, world outline |
| Interaction | 7 | Console/editor commands, undo/redo, key simulation |
| Animation | 7 | AnimMontage, AnimBlueprint, sections, notifies |
| Viewport | 6 | Camera control, screenshots, actor selection, view modes |
| Sequencer | 6 | LevelSequence creation, bindings, tracks, keyframes |
| Input | 6 | Enhanced Input: InputAction, InputMappingContext |
| System | 5 | Server connectivity, metrics, protocol info |
| Project | 4 | Project info, plugins, folders, source files |
| Curve | 4 | Curve assets (Float/LinearColor/Vector) + color atlas |
| UMG | 4 | Widget Blueprint creation, widget tree, properties |
| Debug | 3 | Blueprint breakpoints: set, clear, list |
| RenderTarget | 2 | Texture render targets |
| Physics | 2 | Physical materials (friction/restitution/density) |
| LiveCoding | 2 | Live Coding hot-reload: status query + synchronous compile trigger |

---

## Connection Status Indicator

SmithUE displays a circular status indicator in the editor status bar showing real-time CLI connection state.

### Indicator Colors

| Color | State |
|---|---|
| 🟢 Green | Ready — HTTP server started and port assigned, reachable via CLI |
| 🔴 Gray | Not Ready — Server not started or initializing |

### Interactive Features

- **Tooltip Details**:
  - **SmithUE version** (read from `.uplugin`)
  - **Port** — Current dynamically assigned HTTP port
  - **PID** — Current editor process ID
  - **Status** — Ready state confirmation
- **One-Click Copy**: Click the indicator to copy the base `smithue-cli` command for the current instance.

---

## AI Texture Generation

The `generate_texture` command bridges the editor with modern generative AI. It automatically detects the target API format based on the provided endpoint URL, supporting DALL-E, Imagen, and OpenAI Chat-based generation.

**Example Usage:**
```bash
npx smithue-cli exec generate_texture '{"params":{"prompt":"seamless stylized stone floor, hand-painted style, 4K","endpoint":"https://api.openai.com/v1/images/generations","api_key":"sk-...","model":"dall-e-3","save_path":"/Game/Textures","asset_name":"T_StoneFloor"}}'
```

---

## Changelog

> Full history in [CHANGELOG.md](CHANGELOG.md).

### v1.16.2 (UE5.8, 2026-07-28)
- **Fix partial SKILL deploy**: the panel "重装 SKILL" button and CLI deploy previously copied only `SKILL.md`, missing `reference/` and `scripts/`. `ReinstallSkill()` now recursively copies the whole bundle; `ComputeSkillState()` flags missing `reference/`/`scripts/` as Stale (so the reinstall button appears), self-healing single-file machines. Pairs with smithue-cli 0.14.1.

### v1.16.1 (UE5.8, 2026-07-28)
- Sync with published smithue-cli **0.14.0**: bump `kRecommendedCliVersion` → 0.14.0 (old-CLI machines get the Outdated prompt; SKILL drift self-heals).
- Docs: PITFALLS #7 (consumer-side param passing via `--params-file` / `smithue.ps1`, CJK), #12 (CI auto-publish, bypass-2fa deprecation → OIDC); test report footnote on CLI 0.14.0. Constants + docs only, no tool changes.

### v1.16.0 (UE5.8, 2026-07-22)
- **Upgraded to Unreal Engine 5.8**: API compatibility (material `CountInputs`/`GetMaterialResource(EShaderPlatform)`, `StructUtils/UserDefinedStruct.h`, JSON key `TSharedString`, AnimGraph binding via reflection, scoped `EAutomationTestFlags`, `.uplugin` `PlatformAllowList`, ...), zero deprecation warnings. See [docs/UE5.8-UPGRADE-TEST-REPORT.md](docs/UE5.8-UPGRADE-TEST-REPORT.md).
- **New AI domain (10 tools)**: Blackboard (create/add-key/read), Behavior Tree (create/link-blackboard/read), EQS (create/read), State Tree (create with ComponentSchema + compile / read).
- **New PCG domain (7 tools)**: create PCG graph + in-graph node authoring (`add_pcg_node`/`connect_pcg_nodes`) + generate + level Volume.
- **New Dialog domain (3 tools, worker-safe)**: detect & respond to blocking modal editor dialogs (e.g. `level_save`'s "Save As") even while a modal has jammed the game thread.
- **Blueprint Interface**: `create_blueprint_interface` + `bp_implement_interface`.
- **Anti-overlap node placement + auto-layout**: Material/MaterialFunction/PCG/Blueprint nodes auto-cascade on create; `auto_layout_graph` gains a PCG branch; `get_material_info` returns wiring `edges`.
- **UE5.8 Substrate**: can create `SubstrateSlabBSDF` etc.; `connect_material_pins` supports `dest_input_index=8` (FrontMaterial).
- **Fixes**: Niagara emitter template path (5.8), DataTable multi-row append, `level_save` `level_path` for dialog-free Save-As.
- **314 tools / 29 domains** total.

### v1.15.0 (UE5.2, 2026-07-03)
- **AnimGraph editing tools**: added 8 tools for anim-node property edits, optional pin exposure, member-variable bindings, anim-node reads, and state-machine / state / transition authoring with state-machine read-back.
- **Cross-class Blueprint node fixes**: `bp_create_node` adds `owner_class` for cross-class VariableGet/Set; `target_class` accepts `/Game/...` Blueprint paths and resolves DynamicCast generated `_C` classes.
- **Asset deletion and port maintenance**: `delete_asset force=true` now truly force-deletes and nulls in-memory referencers; portfile prune now uses PID liveness.
- **Release tooling**: `regen-tools.mjs` supports a `SMITHUE_PORT` override for regenerating `TOOLS.md` from another host editor that shares the plugin.

### v1.12.0 (UE5.2, 2026-06-26)
- **Self-describing material tools**: `connect_material_pins` `dest_input_index` now documents `7=WorldPositionOffset` (was 0–6 only); `set_expression_property` lists node-type-specific valid keys and its error echoes the node's valid keys. PITFALLS #15.

### v1.11.0 (UE5.2, 2026-06-25)
- **Startup environment self-check**: detects Node / npm / smithue-cli in the background on editor launch, off the game thread, logging each probe to `LogSmithUE`.
- **"Status & Updates" settings panel**: view environment status, plugin-update reminder (GitHub Releases link), one-click install / upgrade of the CLI; the button adapts to state (Install / Upgrade / Ready / Cancel).
- **Bounded install**: CLI install uses a **120s hard timeout + cancel + npm fast-fail flags** so a bad network never freezes the UI; failures are classified (permission / network / timeout) with a manual fallback.

### v1.10.0
- **Fixed `level_new` crash**: map creation deferred to the next-frame safe point, avoiding world destruction inside the HTTP handler.
- **Added** `spawn_mesh_actor` (mesh + material) and `level_add_basic_env` (one-shot lights / sky / fog / floor).
- **New LiveCoding domain** (`livecoding_status` / `livecoding_compile`).
- Tool count reaches **221 / 24 domains**.

### v0.8.0, CLI Migration
- Removed TCP Server / ConnectionManager / SessionManager; HTTP changed to dynamic port + portfile discovery.
- `/ready` endpoint + 503 startup guard; StatusIndicator rewritten as a CLI-aware dot.
- New `smithue-cli` npm package replaces MCP.

> Earlier versions (v1.x blueprint depth, N-id sessions, metrics, compliance linter, etc.) are in [CHANGELOG.md](CHANGELOG.md).

---

## Roadmap

> **Positioning: different from other UE AI plugins.** Most UE AI plugins stop at the "let AI drive the editor" demo; SmithUE targets real enterprise pain points — **legacy-project collaboration** and **asset compliance standardization** — and lays infrastructure groundwork for **future transcoding / migration standardization**. It delivers not one-off automation scripts, but a **git-trackable, auditable, team-reusable "atomic tool layer + spec layer."**

### Phase 1 (shipped) — Atomic capability foundation
- 24 domains, 229 atomic HTTP tools across Blueprint / AnimGraph / Material / Niagara / Level / Asset / Analysis, etc.
- Spec-driven blueprint factory + compliance linter: specs live as git text in the host project; AI generates compliant blueprints in bulk and audits them in plain language.
- Plugin environment self-check & self-service deployment (startup Node/npm/CLI detection, one-click install/upgrade panel), lowering the barrier for legacy machines / projects / intranet environments.

### Phase 2 — Legacy-project collaboration & compliance standardization
- **Bulk scan & audit of legacy assets**: rule-based health checks on naming / folders / parent class / material slots / LOD / collision, producing trackable compliance reports.
- **Bulk standardization fixes**: regularize existing assets against team specs in one shot; diffs are reviewable and reversible.
- **Team spec collaboration**: spec files committed with the project; everyone shares one compliance baseline; run the linter in CI as a gate.

### Phase 3 — Transcoding / migration standardization groundwork
- **Migration rule engine**: capture "legacy form → standard form" conversions as declarative, reusable rule sets.
- **Cross-version / cross-format asset transcoding pipelines**: repeatable pipelines for engine upgrades, asset-format migration, and standardization refactors.
- **End-to-end audit**: every transcoding / standardization step is logged for enterprise compliance and traceability.

### Phase 4 — Collaboration at scale
- Multi-client concurrent collaboration mode.
- Batch / transactional execution (multiple commands per request, atomic rollback).
- Dashboards & reports: asset health, compliance coverage, migration progress.
- Python SDK / REST OpenAPI / WebSocket real-time event stream.

---

## Known Limitations

- **Concurrency**: Single-client connection only.
- **Input Focus**: `simulate_key` requires viewport focus.
- **Property Types**: Advanced types like `TMap` or delegates are not supported by `set_actor_property`. Nested struct properties require dedicated commands.
- **SceneTexture**: `SceneTextureId` is set via FProperty reflection to avoid link errors with unexported engine symbols.
- **OS**: Windows Win64 only.
- **CLI is an optional consumer**: `smithue-cli` is a convenience client; its install / upgrade depends on the network (with a built-in 120s timeout, cancel, and manual fallback). **The plugin itself works without it.**
- **Engine version**: mainline targets UE 5.2; other versions require adaptation.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for step-by-step instructions on adding new commands.

### AI-Assisted Development

AI coding assistants (OpenCode, Claude Code, Cline, etc.) should read these repo-root files:

- **[AGENTS.md](AGENTS.md)** — repo boundaries, build commands, test systems, pitfalls
- **[docs/spec/](docs/spec/)** — tool authoring conventions (TOOL_SPEC + NAMING + PITFALLS)

The runtime `smithue-control` skill (for operating the editor, not development) ships with [smithue-cli](https://www.npmjs.com/package/smithue-cli) (`smithue-cli skill --install`).

---

## Historical: MCP Server Deprecated
The MCP Server was removed in v0.8.0 and replaced by `smithue-cli`. See [smithue-cli MIGRATION.md](https://github.com/123dx-svg/smithue-cli/blob/main/MIGRATION.md) for the migration guide.

---

## License

Copyright 2026, 123dx-svg. MIT License.
