# SmithUE 插件发版 Spec（UBT 重编 + 打包 + GitHub Release）

> 给维护者 / AI 代理。发 SmithUE 插件（GitHub Releases）前**必读**。把经过验证的发版流程固定下来，照着走即可，避免漏掉重编、打包剔除、TOOLS.md 重生成、分支等坑。
>
> 占位符同 [AGENTS.md](../../AGENTS.md)：`{EngineRoot}` = UE 5.2 安装根、`{ProjectRoot}` = 宿主工程根、`{Project}` = 宿主工程名（当前环境 `{Project}` = AIScript）。**勿把绝对路径写死。**

## 0. 前提

- 仓库 `123dx-svg/SmithUE`，**默认分支 `UE5.2`（不是 main）**。
- 与 `smithue-cli` 版本号**完全独立**，禁止比较 / 对齐。
- Release tag 约定 **`vX.Y.Z-UE5.2`**；资产命名 **`SmithUE-vX.Y.Z-UE5.2-Win64.zip`**。
- 编译目标是 **`{Project}Editor`**（宿主 target，当前即 `AIScriptEditor`），不是 "SmithUE"。

## 1. 关键坑（必读）

1. **编辑器锁 DLL** → 重编 / 打包前**必须先关编辑器**（`Stop-Process -Name UnrealEditor -Force`）。
2. UBT 误报 "Target is up to date" 不重编 → 删 `{ProjectRoot}\Intermediate\Build\Win64\x64\{Project}Editor\Development\ActionHistory.bin`。
3. **CJK 文件（CHANGELOG / README / docs）用 Edit/Write，绝不用 PowerShell `Set-Content`**（PS 5.1 按 GBK 误读 UTF-8 → 乱码）。
4. 提交信息用 `git commit -F <file>`，信息文件用 Write 写（PS 多行 `-m` / CJK 会坏）。
5. **TOOLS.md 不手写**：改了工具 description / 参数后用 `node scripts/regen-tools.mjs`（拉运行中编辑器的 `/api/v1/tools` 重生成）。
6. 改了 C++ 源码 → **打包前务必重编**，让 `Binaries/Win64/UnrealEditor-SmithUE.dll` 是最新（否则 zip 里的 DLL 与源码 / TOOLS.md 不一致）。
7. 打包**剔除** `.pdb`（调试符号 ~80MB）、`*.patch_*`（Live Coding 临时）、`.git/.omo/.codegraph/Intermediate/.github` → zip 约 1.5MB；**保留** `Binaries/Win64/UnrealEditor-SmithUE.dll` + `UnrealEditor.modules`。
8. **描述写工具契约，不写 UE 版本特有 API**（见 [TOOL_SPEC §3.1](TOOL_SPEC.md) / [PITFALLS #15](PITFALLS.md)）。

## 2. 版本号（semver）

- `SmithUE.uplugin` 的 `VersionName`（语义版本）+ `Version`（整数，每次 +1）**同步 bump**。
- patch = fix / doc · minor = 新工具 / 特性（兼容）· major = 破坏性。

## 3. 标准发版流程：关 → 编 → 启 → 验 → bump → 提交 → 重生成 TOOLS.md → 打包 → zip → release

### 3.1 关编辑器 + UBT 重编（有源码改动时）
```powershell
Stop-Process -Name UnrealEditor -Force -EA SilentlyContinue; Start-Sleep -Seconds 3
$ah = "{ProjectRoot}\Intermediate\Build\Win64\x64\{Project}Editor\Development\ActionHistory.bin"
if (Test-Path $ah) { Remove-Item $ah -Force }
dotnet "{EngineRoot}\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" `
  {Project}Editor Win64 Development "{ProjectRoot}\{Project}.uproject" -WaitMutex
# 退出码 0 才继续
```

### 3.2 重启 + 运行态验证
清 `{ProjectRoot}\Saved\Autosaves\*` → 启动 `{EngineRoot}\Engine\Binaries\Win64\UnrealEditor.exe "{ProjectRoot}\{Project}.uproject"` → 轮询 `/ready`（端口动态：读 `%LOCALAPPDATA%\.smithue\<pid>.port`，按 `project_name` 过滤）。用 smithue-cli / Node HTTP 验证本次改动真的生效。

### 3.3 bump 版本（3 处，CJK 用 Edit）
- `SmithUE.uplugin`：`VersionName` + `Version`。
- `CHANGELOG.md`：`## 未发布（当前 UE5.2 分支）` → `## vX.Y.Z（UE5.2，YYYY-MM-DD）`。
- `README.md` / `README.en.md`：变更日志区加 `### vX.Y.Z` 条目。

> **`kRecommendedCliVersion` 仅表示 CLI 兼容下限，不必随每次 CLI 发版提高。** 环境探针会查询 npm 最新版本，取其与兼容下限的较高者判断是否过期；网络失败时回退兼容下限。默认开启的 `bAutoUpgradeCliOnStartup` 会在检测到 CLI 未安装或过期时，每次编辑器会话最多自动安装 / 升级一次，由 npm `postinstall` 部署配套 SKILL；用户可在「Status & Updates」关闭。面板同时比对已部署 SKILL 与已装 CLI bundle，并检查配套目录是否缺失，过期时提供完整技能包重装。只有插件确实需要更新的 CLI 才提高兼容下限；此常量与插件版本号无关。

### 3.4 重生成 TOOLS.md（工具描述 / 参数有变时）
```powershell
node scripts/regen-tools.mjs        # 拉运行中编辑器的 /api/v1/tools
git diff -- TOOLS.md                # 校验只有预期工具块变化、计数不变
```

### 3.5 提交 + 推送
```powershell
git add SmithUE.uplugin CHANGELOG.md README.md README.en.md TOOLS.md <改动的源码>
git commit -F <msgfile>
git push origin UE5.2
```

### 3.6 打包到 Saved
```powershell
$stage = "{ProjectRoot}\Saved\SmithUE"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
robocopy "{ProjectRoot}\Plugins\SmithUE" $stage /E `
  /XD .git .omo .codegraph Intermediate .github `
  /XF *.pdb *.exp *.lib *.patch_* *.suppressed.* /NFL /NDL /NJH /NJS /NP
# robocopy 退出码 < 8 即成功
```
校验：`$stage\SmithUE.uplugin` 版本正确；`$stage\Binaries\Win64\` 只剩 `UnrealEditor-SmithUE.dll` + `UnrealEditor.modules`。

### 3.7 zip（根目录须为 SmithUE/）
```powershell
Compress-Archive -Path "{ProjectRoot}\Saved\SmithUE" `
  -DestinationPath "{ProjectRoot}\Saved\SmithUE-vX.Y.Z-UE5.2-Win64.zip" -CompressionLevel Optimal
```
zip 根应为 `SmithUE/`，用户可直接解压进宿主 `Plugins/`。

### 3.8 GitHub Release（notes 用 Write 写，CJK 安全）
```powershell
gh release create vX.Y.Z-UE5.2 `
  --repo 123dx-svg/SmithUE --target UE5.2 `
  --title "SmithUE vX.Y.Z (UE 5.2)" `
  --notes-file <notes.md> --latest `
  "{ProjectRoot}\Saved\SmithUE-vX.Y.Z-UE5.2-Win64.zip"
gh release view vX.Y.Z-UE5.2 --repo 123dx-svg/SmithUE   # 核验 Latest + 资产
```

## 4. 与 CLI 发版的区别（勿混淆）

- 本流程**只管插件（GitHub Releases）**。
- **smithue-cli（npm）**走另一套：`npm version` → `npm publish --registry https://registry.npmjs.org`。详见 [smithue-cli `docs/RELEASE.md`](https://github.com/123dx-svg/smithue-cli/blob/main/docs/RELEASE.md)。
- 两者**版本号独立递增**，发版时机互不依赖。
