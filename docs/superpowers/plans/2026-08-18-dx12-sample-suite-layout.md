# DX12 Sample Suite 目录重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有 Geometry Pipeline Demo 迁入独立 Sample 项目，并保持 VS2022、Vertex/Mesh、Debug/Release 全部可运行。

**Architecture:** 根 Solution 作为多个 DX12 Sample 的容器；`samples/01-GeometryPipeline` 拥有当前 Demo 的源码和 Shader。`Directory.Build.props` 统一工具链及按项目隔离的输出路径；公共库延后到第二个 Sample 出现时提取。

**Tech Stack:** C++17、Win32、Direct3D 12、HLSL、DXC、Visual Studio 2022、MSBuild、PowerShell。

**Spec:** `docs/superpowers/specs/2026-08-18-dx12-sample-suite-layout-design.md`

## Global Constraints

- 工作目录为 `F:\cc-workspace\my-engine-demo`。
- 保留 `.sln + .vcxproj`，不使用 CMake。
- 当前渲染行为和命令行参数不变。
- Sample 输出目录必须包含项目名，避免未来项目产物冲突。
- 本次不创建 `Dx12Common`，不修改 D3D12 渲染逻辑。

---

### Task 1: 用新产物路径建立 RED

**Files:**
- Modify: `tests/Smoke.Tests.ps1`

**Interfaces:**
- Produces: `x64/<Configuration>/GeometryPipelineDemo/GeometryPipelineDemo.exe` runtime contract.

- [ ] **Step 1: 将测试 EXE 路径改为新目录**

```powershell
$executablePath = Join-Path $repositoryRoot "x64\$Configuration\GeometryPipelineDemo\GeometryPipelineDemo.exe"
```

- [ ] **Step 2: 运行 Debug Smoke Test 并确认 RED**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：因新路径下 EXE 尚不存在而失败。

---

### Task 2: 迁移 Geometry Pipeline 项目

**Files:**
- Create: `Directory.Build.props`
- Move: `my-engine-demo.vcxproj` → `samples/01-GeometryPipeline/GeometryPipelineDemo.vcxproj`
- Move: `src/*` → `samples/01-GeometryPipeline/src/*`
- Move: `shaders/*` → `samples/01-GeometryPipeline/shaders/*`
- Modify: `my-engine-demo.sln`
- Modify: `samples/01-GeometryPipeline/GeometryPipelineDemo.vcxproj`

**Interfaces:**
- Produces: VS project `GeometryPipelineDemo`.
- Produces: EXE and CSO files under `x64/<Configuration>/GeometryPipelineDemo/`.

- [ ] **Step 1: 创建公共 MSBuild 属性**

`Directory.Build.props` 固定 SDK、v143、C++17，并设置：

```xml
<OutDir>$(SolutionDir)x64\$(Configuration)\$(MSBuildProjectName)\</OutDir>
<IntDir>$(SolutionDir)x64\$(Configuration)\$(MSBuildProjectName)\obj\</IntDir>
```

- [ ] **Step 2: 机械迁移源码、Shader 与项目文件**

迁移后不保留根 `src/`、`shaders/` 和旧 VCXPROJ。

- [ ] **Step 3: 更新 Solution 与 VCXPROJ 路径**

Solution 项目名改为 `GeometryPipelineDemo`；VCXPROJ 内部源文件和 Shader 路径保持相对项目目录，TargetName 改为 `GeometryPipelineDemo`。

- [ ] **Step 4: 构建 Debug 并运行 Smoke Test**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：0警告、0错误，Smoke Test 通过。

---

### Task 3: 更新入口与完成验收

**Files:**
- Modify: `run-vertex.cmd`
- Modify: `run-mesh.cmd`
- Modify: `README.md`

**Interfaces:**
- Produces: root launch scripts targeting `GeometryPipelineDemo.exe`.
- Produces: repository-level Sample Suite documentation.

- [ ] **Step 1: 更新两个启动脚本**

Debug-first 路径改为：

```text
x64\Debug\GeometryPipelineDemo\GeometryPipelineDemo.exe
```

Release fallback 使用相同项目目录结构。

- [ ] **Step 2: 更新 README**

说明 Sample Suite 拓扑、当前 Sample、独立输出目录、VS F5 和未来扩展规则。

- [ ] **Step 3: 验证脚本、Debug 与 Release**

```powershell
.\run-vertex.cmd --frames 3
.\run-mesh.cmd --frames 3
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Release
```

Debug/Release 均先 Rebuild；要求0警告、0错误且所有命令返回0。

- [ ] **Step 4: 检查仓库边界并提交**

```powershell
git diff --check
git status --short
git ls-files | rg '(^|/)(x64|\.vs)/|\.(cso|exe|obj|pdb)$'
```

要求没有构建产物被跟踪，旧根源码目录和 VCXPROJ 已移除。

