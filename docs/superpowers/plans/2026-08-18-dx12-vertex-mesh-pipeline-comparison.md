# DX12 Vertex Shader 与 Mesh Shader 对照示例 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个可由 VS2022 直接打开的 DX12 x64 示例程序，通过 `--pipeline vertex` 与 `--pipeline mesh` 分别用 `DrawIndexedInstanced` 和 `DispatchMesh` 渲染同一个旋转彩色三角形。

**Architecture:** 一个 Win32/DX12 可执行程序共享窗口、交换链、几何资源、常量缓冲与 Pixel Shader，只在完整三角形形成之前分为传统 IA/VS 路径和 Mesh Shader Thread Group 路径。Vertex/Index Default Heap Buffer 同时作为传统 VB/IB 与 Mesh Shader Root SRV 使用；两条路径在 Rasterizer 前汇合。

**Tech Stack:** C++17、Win32、DXGI 1.6、Direct3D 12、HLSL、DXC、Visual Studio 2022 Community、Windows SDK 10.0.26100.0、PowerShell smoke tests。

**Spec:** `docs/superpowers/specs/2026-08-18-dx12-vertex-mesh-pipeline-comparison-design.md`

## Global Constraints

- 工作目录固定为 `F:\cc-workspace\my-engine-demo`。
- 工程形式固定为 `.sln + .vcxproj`，不使用 CMake。
- 只生成一个 `my-engine-demo.exe`，运行模式由 `--pipeline vertex|mesh` 选择。
- 目标平台固定为 `x64`，配置为 `Debug` 和 `Release`。
- Windows SDK 固定为 `10.0.26100.0`；C++ 标准固定为 C++17。
- 本机 MSBuild 路径为 `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe`。
- 不引入第三方库，不使用 `d3dx12.h`；D3D12 结构体和 Pipeline State Stream 辅助类型在项目内定义。
- 不实现材质、Texture、Sampler、光照、Depth Buffer、相机、双 Viewport、Amplification Shader 或 Meshlet 构建。
- 两条管线复用同一份 Vertex/Index GPU Buffer 和同一个 `PixelShader.cso`。
- Mesh 模式不支持时返回明确错误，不回退到 Vertex 模式。
- 所有自动运行都带 `--frames N`；自动模式不显示模态对话框。
- 每个生产行为先由失败测试或失败的 smoke case 驱动，再实现最小代码使其通过。

---

### Task 1: 建立 VS2022 工程与命令行契约

**Files:**
- Create: `.gitignore`
- Create: `my-engine-demo.sln`
- Create: `my-engine-demo.vcxproj`
- Create: `src/PipelineMode.h`
- Create: `src/PipelineMode.cpp`
- Create: `src/Main.cpp`
- Create: `tests/Smoke.Tests.ps1`

**Interfaces:**
- Produces: `enum class PipelineMode { Vertex, Mesh };`
- Produces: `struct AppOptions { PipelineMode pipeline; std::optional<uint64_t> frameLimit; };`
- Produces: `AppOptions ParseOptions(const std::vector<std::wstring>& args);`
- Produces: `std::wstring BuildUsageText();`
- Produces: process exit codes `0=success`, `2=invalid arguments`, `3=selected pipeline not implemented`, `4=runtime failure`, `5=mesh unsupported`, `6=debug validation failure`.

- [ ] **Step 1: 写第一个失败的 smoke test**

创建 `tests/Smoke.Tests.ps1`，接收可选参数 `-Configuration Debug`，将 EXE 定位到 `x64\<Configuration>\my-engine-demo.exe`。先只声明以下契约：

```powershell
Assert-ExitCode @('--pipeline', 'unknown', '--frames', '1') 2
Assert-ExitCode @('--pipeline', 'vertex', '--frames', 'bad') 2
Assert-ExitCode @('--frames', '1') 2
```

`Assert-ExitCode` 使用 `Start-Process -Wait -PassThru`，不得依赖标准输出内容。

- [ ] **Step 2: 运行测试并确认 RED**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：测试因 `x64\Debug\my-engine-demo.exe` 不存在而失败，失败原因是产品尚未创建。

- [ ] **Step 3: 创建最小 Solution 与 VCXPROJ**

创建只包含一个 C++ 项目的 `my-engine-demo.sln`，以及支持 `Debug|x64`、`Release|x64` 的 `my-engine-demo.vcxproj`：

- `ConfigurationType=Application`；
- `PlatformToolset=v143`；
- `WindowsTargetPlatformVersion=10.0.26100.0`；
- `LanguageStandard=stdcpp17`；
- `SubSystem=Windows`；
- `OutDir=$(SolutionDir)x64\$(Configuration)\`；
- `IntDir=$(SolutionDir)x64\$(Configuration)\obj\`；
- 链接 `user32.lib`、`shell32.lib`、`d3d12.lib`、`dxgi.lib`、`dxguid.lib`。

此阶段先不加入 HLSL Custom Build Item。

同时创建 `.gitignore`，至少忽略 `.vs/`、`x64/`、`*.user` 和 `*.suo`。

- [ ] **Step 4: 实现参数解析的最小代码**

`ParseOptions` 的规则：

- `--pipeline` 为必填，只接受 `vertex` 或 `mesh`；
- `--frames` 可选，只接受正整数；
- 未知参数、重复参数、缺值均抛出 `std::invalid_argument`；
- 参数列表不包含 EXE 路径。

`wWinMain` 使用 `CommandLineToArgvW` 转换参数。参数错误写入 `OutputDebugStringW` 并返回 2，不显示 MessageBox。合法参数暂时返回 3，作为后续渲染测试的明确 RED 状态。

- [ ] **Step 5: 构建 Debug x64**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64
```

预期：构建退出码 0，无编译错误。

- [ ] **Step 6: 运行 smoke test 并确认 GREEN**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：3个非法参数 case 全部得到退出码 2；测试退出码 0。

- [ ] **Step 7: 提交 Task 1**

```powershell
git add .gitignore my-engine-demo.sln my-engine-demo.vcxproj src tests
git commit -m "build: 建立 DX12 示例工程与命令行契约"
```

---

### Task 2: 实现公共 DX12 Runtime 与 Vertex Shader 路径

**Files:**
- Create: `src/TriangleData.h`
- Create: `src/TriangleData.cpp`
- Create: `src/D3D12App.h`
- Create: `src/D3D12App.cpp`
- Create: `shaders/Common.hlsli`
- Create: `shaders/VertexPipeline.hlsl`
- Create: `shaders/PixelShader.hlsl`
- Modify: `src/Main.cpp`
- Modify: `my-engine-demo.vcxproj`
- Modify: `tests/Smoke.Tests.ps1`

**Interfaces:**
- Produces: `struct TriangleVertex { float position[2]; float color[3]; };`
- Produces: `const std::array<TriangleVertex, 3>& TriangleVertices();`
- Produces: `const std::array<uint32_t, 3>& TriangleIndices();`
- Produces: `class D3D12App` with `Initialize()`, `RenderFrame(float elapsedSeconds)`, `WaitForGpu()`, `HasDebugValidationErrors()`.
- Consumes: `AppOptions` and `PipelineMode` from Task 1.
- Produces: `VertexShader.cso` and `PixelShader.cso` beside the EXE under `shaders\`.

- [ ] **Step 1: 增加 Vertex 路径失败测试**

在 `Smoke.Tests.ps1` 中加入：

```powershell
Assert-ExitCode @('--pipeline', 'vertex', '--frames', '3') 0
```

- [ ] **Step 2: 运行测试并确认 RED**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：Vertex case 得到退出码 3，而不是 0；非法参数 case 仍通过。

- [ ] **Step 3: 定义公共三角形数据和 Shader ABI**

`TriangleData` 返回3个 Position/Color Vertex 和索引 `{0,1,2}`。`Common.hlsli` 定义：

```hlsl
struct VertexData {
    float2 position;
    float3 color;
};

struct RasterVertex {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

cbuffer SceneConstants : register(b0) {
    float2 rotation; // cos(angle), sin(angle)
};
```

Vertex C++ stride 固定为20字节，Input Layout 使用：

- `POSITION`: `DXGI_FORMAT_R32G32_FLOAT`, offset 0；
- `COLOR`: `DXGI_FORMAT_R32G32B32_FLOAT`, offset 8。

- [ ] **Step 4: 写 Vertex 与公共 Pixel Shader**

`VertexPipeline.hlsl` 定义带 `POSITION`、`COLOR0` 语义的 `VSInput`，再由 `VSMain` 对 Position 应用二维旋转，输出 `RasterVertex`。`PSMain` 只返回插值后的 Color，不读取任何 Texture 或其他资源。用于 StructuredBuffer 的 `VertexData` 不直接作为 IA 的入口参数，避免把 Buffer 内存布局与 IA 语义混为一体。

- [ ] **Step 5: 为 VS/PS 增加 DXC 构建规则**

在 `.vcxproj` 中用 Custom Build Item 调用：

```text
$(WindowsSdkVerBinPath)x64\dxc.exe
```

命令显式加入 `-I "$(ProjectDir)shaders"`，源文件使用 `%(FullPath)`，避免 MSBuild 工作目录变化导致 `Common.hlsli` 无法解析。

编译命令分别为：

```text
-T vs_6_0 -E VSMain VertexPipeline.hlsl -Fo $(OutDir)shaders\VertexShader.cso
-T ps_6_0 -E PSMain PixelShader.hlsl   -Fo $(OutDir)shaders\PixelShader.cso
```

为两个输出声明 `Outputs`，把 `Common.hlsli` 列为 `AdditionalInputs`。构建前确保 `$(OutDir)shaders` 存在。

- [ ] **Step 6: 实现 D3D12 公共初始化**

`D3D12App::Initialize()` 按以下顺序实现：

1. Debug 构建尝试取得 `ID3D12Debug` 并启用 Debug Layer；失败只写警告。
2. 创建 `IDXGIFactory6`，使用 `EnumAdapterByGpuPreference(...HIGH_PERFORMANCE...)` 选择非软件 Adapter。
3. 以 `D3D_FEATURE_LEVEL_12_0` 创建 Device，并查询 `ID3D12Device2`。
4. 创建 Direct Command Queue、双缓冲 SwapChain、RTV Heap 与两个 RTV。
5. 创建每 Back Buffer 一个 Command Allocator、一个 `ID3D12GraphicsCommandList6`、Fence 和 Event。
6. 设置单一 Viewport、Scissor Rect 和 Clear Color。

所有 HRESULT 通过项目内 `ThrowIfFailed(HRESULT, const char* stage)` 转为包含阶段信息的异常。

- [ ] **Step 7: 创建共享 Root Signature 与常量缓冲**

使用 Root Signature 1.1，参数顺序固定为：

```text
Root parameter 0: CBV b0
Root parameter 1: SRV t0
Root parameter 2: SRV t1
```

使用 `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`。不要使用 Descriptor Heap。常量缓冲使用 persistently mapped Upload Heap，分配大小按256字节对齐，保存 `cos(angle)` 和 `sin(angle)`。

- [ ] **Step 8: 上传并复用 Vertex/Index Buffer**

创建两个 Default Heap Buffer 和对应 Upload Buffer，通过 Copy Command 上传。最终状态固定为：

```text
Vertex: VERTEX_AND_CONSTANT_BUFFER | NON_PIXEL_SHADER_RESOURCE
Index:  INDEX_BUFFER              | NON_PIXEL_SHADER_RESOURCE
```

生成 VBV/IBV，同时保留两个 Default Buffer 的 GPU Virtual Address，供 Task 3 的 Root SRV 使用。初始化复制完成后等待 GPU，再释放临时 Upload Buffer。

- [ ] **Step 9: 创建 Vertex Graphics PSO**

读取 `VertexShader.cso` 与 `PixelShader.cso`，填充 `D3D12_GRAPHICS_PIPELINE_STATE_DESC`：

- 公共 Root Signature；
- Triangle Input Layout；
- `D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE`；
- `D3D12_CULL_MODE_NONE`；
- Depth/Stencil disabled；
- 一个 `DXGI_FORMAT_R8G8B8A8_UNORM` Render Target；
- Sample Count 1。

- [ ] **Step 10: 记录并提交 Vertex Draw**

每帧：

1. Reset 当前 Frame Allocator 与 Command List。
2. Back Buffer `PRESENT → RENDER_TARGET`。
3. 设置 Root Signature、Vertex PSO、Viewport、Scissor、RTV。
4. Clear RTV，绑定 CBV、VBV、IBV 和 `D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST`。
5. 调用 `DrawIndexedInstanced(3, 1, 0, 0, 0)`。
6. Back Buffer `RENDER_TARGET → PRESENT`。
7. Execute、Present，并用 Fence 管理 Frame Allocator 重用。

- [ ] **Step 11: 接入 Win32 消息循环与有限帧退出**

`Main.cpp` 创建窗口并运行非阻塞消息循环。每次渲染前更新旋转常量；成功 Present 后递增 frame count。达到 `--frames N` 后等待 GPU 并正常退出。自动模式发生异常时只写 `OutputDebugStringW` 并返回 4。

- [ ] **Step 12: 构建并运行 Vertex smoke test**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：Vertex case 运行3帧后退出码 0；非法参数 case 保持通过。

- [ ] **Step 13: 检查 Debug Layer 并重构**

Debug Layer 可用时，通过 `ID3D12InfoQueue` 统计 Error/Corruption 消息。有限帧退出前若发现此类消息返回6。保持 test green 后，只进行命名、重复代码和资源释放顺序整理。

- [ ] **Step 14: 提交 Task 2**

```powershell
git add my-engine-demo.vcxproj src shaders tests
git commit -m "feat: 实现 DX12 Vertex Shader 三角形路径"
```

---

### Task 3: 实现 Mesh Shader Thread Group 路径

**Files:**
- Create: `shaders/MeshPipeline.hlsl`
- Modify: `src/D3D12App.h`
- Modify: `src/D3D12App.cpp`
- Modify: `my-engine-demo.vcxproj`
- Modify: `tests/Smoke.Tests.ps1`

**Interfaces:**
- Consumes: Root parameters `b0/t0/t1` and shared Vertex/Index buffers from Task 2.
- Produces: `MeshShader.cso` compiled as `ms_6_5`.
- Produces: `D3D12App::CreateMeshPipeline()` and `D3D12App::RecordMeshDraw()`.
- Produces: Mesh capability failure mapped to process exit code 5.

- [ ] **Step 1: 增加 Mesh 路径失败测试**

在 `Smoke.Tests.ps1` 中加入：

```powershell
Assert-ExitCode @('--pipeline', 'mesh', '--frames', '3') 0
```

- [ ] **Step 2: 运行测试并确认 RED**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：Mesh case 得到退出码 3，而不是 0；Vertex 与非法参数 case 仍通过。

- [ ] **Step 3: 编写 Mesh Shader**

`MeshPipeline.hlsl` 引用 `Common.hlsli`，定义：

```hlsl
StructuredBuffer<VertexData> vertexBuffer : register(t0);
StructuredBuffer<uint> indexBuffer : register(t1);
groupshared VertexData sharedVertices[3];
```

`MSMain` 的约束：

- `[outputtopology("triangle")]`；
- `[numthreads(32, 1, 1)]`；
- 一致控制流中调用一次 `SetMeshOutputCounts(3, 1)`；
- Thread 0～2 将 Vertex SRV 读入 `sharedVertices`；
- 全部32个 Thread 调用 `GroupMemoryBarrierWithGroupSync()`；
- Thread 0～2写3个 `RasterVertex`；
- Thread 0从 Index SRV 读取3个 Index，并一次性写入 `uint3 primitiveIndices[0]`。

- [ ] **Step 4: 增加 Mesh Shader DXC 规则**

```text
-T ms_6_5 -E MSMain MeshPipeline.hlsl -Fo $(OutDir)shaders\MeshShader.cso
```

把 `Common.hlsli` 列为 Additional Input，并声明 `MeshShader.cso` 为输出。

- [ ] **Step 5: 增加 Mesh Shader 能力检查**

仅在 `PipelineMode::Mesh` 下调用：

```cpp
D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
```

若 `MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED`，抛出可识别的 `MeshShaderUnsupported` 错误；`Main.cpp` 捕获后返回5。不要创建 Vertex PSO 作为回退。

- [ ] **Step 6: 定义本地 Pipeline State Stream 类型**

不使用 `d3dx12.h`。在 `D3D12App.cpp` 内定义 `alignas(void*)` 的 typed subobject wrapper，并建立只包含必要子对象的 Mesh Pipeline State Stream：

- Root Signature；
- Mesh Shader bytecode；
- 公共 Pixel Shader bytecode；
- Blend、Rasterizer、DepthStencil；
- Sample Mask；
- Primitive Topology Type；
- Render Target Format；
- Sample Desc。

使用 `ID3D12Device2::CreatePipelineState(D3D12_PIPELINE_STATE_STREAM_DESC, ...)` 创建 Mesh PSO。

- [ ] **Step 7: 记录 DispatchMesh**

Mesh 模式每帧复用 Task 2 的公共清屏、Barrier 和 Present 流程，只替换几何提交部分：

```text
SetGraphicsRootConstantBufferView(0, constantBufferGpuVA)
SetGraphicsRootShaderResourceView(1, vertexBufferGpuVA)
SetGraphicsRootShaderResourceView(2, indexBufferGpuVA)
ID3D12GraphicsCommandList6::DispatchMesh(1, 1, 1)
```

Mesh 路径不得调用 `IASetVertexBuffers`、`IASetIndexBuffer` 或 `DrawIndexedInstanced`。

- [ ] **Step 8: 构建并运行全部 smoke tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

预期：Vertex 和 Mesh 各运行3帧后返回0；非法参数 case 返回2；测试整体返回0。

- [ ] **Step 9: 检查执行路径和 Debug InfoQueue**

为两个命令分别运行有限帧模式，确认窗口标题包含 `Vertex Shader` 或 `Mesh Shader`。Debug InfoQueue 不包含 Error/Corruption；Mesh 模式能力查询结果不是 `NOT_SUPPORTED`。

- [ ] **Step 10: 提交 Task 3**

```powershell
git add my-engine-demo.vcxproj src shaders tests
git commit -m "feat: 实现 DX12 Mesh Shader 三角形路径"
```

---

### Task 4: 增加启动脚本与教学文档

**Files:**
- Create: `run-vertex.cmd`
- Create: `run-mesh.cmd`
- Create: `README.md`

**Interfaces:**
- Consumes: `my-engine-demo.exe --pipeline vertex|mesh` from Tasks 2-3.
- Produces: 双击可用的 Debug-first 启动脚本；Debug EXE 不存在时尝试 Release EXE。
- Produces: 面向 VS2022 用户的构建、运行和数据路径说明。

- [ ] **Step 1: 编写两个启动脚本**

两个脚本从 `%~dp0` 定位仓库根目录，优先运行 `x64\Debug\my-engine-demo.exe`，不存在时运行 `x64\Release\my-engine-demo.exe`，两者都不存在时输出构建提示并返回非零退出码。

- [ ] **Step 2: 编写 README**

README 保持在本示例边界内，包含：

- VS2022、Windows SDK、RTX Mesh Shader 支持要求；
- 打开 `my-engine-demo.sln`，选择 `x64/Debug` 或 `x64/Release`；
- 两条命令与两个脚本；
- `DrawIndexedInstanced` 与 `DispatchMesh` 数据路径对照；
- Grid/Thread Group/Warp/Thread、Register、Groupshared、SM 的对应关系；
- 两条路径在完整 Primitive 后共享 Rasterizer 与 Pixel Shader；
- 为什么不加入材质和 Texture；
- Mesh Shader 不支持时的错误行为。

- [ ] **Step 3: 人工检查无限帧交互模式**

分别运行：

```powershell
.\x64\Debug\my-engine-demo.exe --pipeline vertex
.\x64\Debug\my-engine-demo.exe --pipeline mesh
```

每次检查三角形持续旋转、颜色一致、窗口关闭正常。第二个模式可以在关闭第一个后重启，不要求同屏比较。

- [ ] **Step 4: 提交 Task 4**

```powershell
git add README.md run-vertex.cmd run-mesh.cmd
git commit -m "docs: 补充 DX12 管线对照运行说明"
```

---

### Task 5: 完整构建与验收

**Files:**
- Verify only: all project files

**Interfaces:**
- Consumes: all deliverables from Tasks 1-4.
- Produces: fresh Debug/Release build evidence and Vertex/Mesh runtime evidence.

- [ ] **Step 1: 清理并构建 Debug x64**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

检查退出码0，并确认以下文件存在：

```text
x64\Debug\my-engine-demo.exe
x64\Debug\shaders\VertexShader.cso
x64\Debug\shaders\MeshShader.cso
x64\Debug\shaders\PixelShader.cso
```

- [ ] **Step 2: 运行 Debug smoke tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
```

要求全部 case 通过，Vertex/Mesh 有限帧运行退出码0，D3D12 InfoQueue 无 Error/Corruption。

- [ ] **Step 3: 清理并构建 Release x64**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

检查退出码0，并确认 EXE 与三个 CSO 存在。

- [ ] **Step 4: 运行 Release smoke tests**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Release
```

要求全部 case 通过。

- [ ] **Step 5: 检查仓库状态和变更边界**

```powershell
git diff --check
git status --short
git log --oneline --decorate -8
```

确认没有构建产物、`.vs/`、`x64/` 或一次性缓存被跟踪；必要时补充 `.gitignore` 并提交。

- [ ] **Step 6: 对照 Spec 逐项验收**

逐项确认：

- 一个 Solution、一个应用项目、一个 EXE；
- Vertex 命令实际提交 `DrawIndexedInstanced`；
- Mesh 命令实际提交 `DispatchMesh`；
- 两条路径使用相同 GPU 几何资源和公共 Pixel Shader；
- 不包含材质、Texture、Sampler、Depth Buffer 或双 Viewport；
- VS2022 可打开工程，Debug/Release x64 构建成功；
- README 与启动脚本存在。

- [ ] **Step 7: 在完成声明前执行代码审查与新鲜验证**

按 `superpowers:requesting-code-review` 检查实现是否符合 Spec；修正发现后重新执行 Tasks 5.1～5.4。只有最新一轮构建和测试输出均为0时才能报告完成。
