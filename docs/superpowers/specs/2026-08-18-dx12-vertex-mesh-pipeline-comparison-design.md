# DX12 Vertex Shader 与 Mesh Shader 对照示例设计

## 背景

本项目用于直观比较 Direct3D 12 传统 Vertex Shader 几何入口与 Mesh Shader 几何入口。两种模式渲染相同的旋转三角形，并在完整三角形图元进入固定功能后端前汇合。

示例聚焦几何工作的组织方式，不引入材质、纹理、光照、深度缓冲、相机系统或双 Viewport。

## 目标

- 提供可由 Visual Studio 2022 直接打开和编译的 `.sln + .vcxproj` 工程。
- 生成一个 `my-engine-demo.exe`。
- 通过命令行参数选择传统 Vertex Shader 或 Mesh Shader 管线。
- 使用相同三角形数据、旋转变换、Viewport、Rasterizer State、Pixel Shader 逻辑和 Render Target。
- 明确展示两条路径在图元生成前的差异，以及进入 Rasterizer 后的共同路径。
- 在本机 RTX 4080 SUPER 与 Windows SDK 10.0.26100.0 环境中完成构建和运行验证。

## 非目标

- 不实现材质、Texture、Sampler、光照和阴影。
- 不实现双 Viewport 或同帧并排比较。
- 不实现完整 Meshlet 离线构建、GPU Driven Rendering、Amplification Shader 或 Nanite 类管线。
- 不引入 ImGui、DirectXTK、CMake 或其他第三方依赖。
- 不讨论 Mesh Shader 输出后的未公开 NVIDIA 物理实现边界。

## 用户入口

程序支持以下启动方式：

```powershell
my-engine-demo.exe --pipeline vertex
my-engine-demo.exe --pipeline mesh
```

用于自动验证的有限帧运行方式：

```powershell
my-engine-demo.exe --pipeline vertex --frames 3
my-engine-demo.exe --pipeline mesh --frames 3
```

默认不指定 `--frames` 时持续运行，直到用户关闭窗口。窗口标题显示当前管线模式。

`run-vertex.cmd` 和 `run-mesh.cmd` 分别封装两种启动命令，便于重启切换。

## 工程结构

```text
my-engine-demo.sln
my-engine-demo.vcxproj
src/
├─ Main.cpp
├─ D3D12App.h
├─ D3D12App.cpp
├─ PipelineMode.h
├─ PipelineMode.cpp
├─ TriangleData.h
└─ TriangleData.cpp
shaders/
├─ Common.hlsli
├─ VertexPipeline.hlsl
├─ MeshPipeline.hlsl
└─ PixelShader.hlsl
tests/
└─ Smoke.Tests.ps1
run-vertex.cmd
run-mesh.cmd
README.md
```

职责划分：

- `Main.cpp`：Win32 入口、窗口创建、消息循环和异常展示。
- `D3D12App`：DXGI Adapter、D3D12 Device、SwapChain、Command Queue、Command List、Fence、Render Target、Buffer、Root Signature 与 PSO 生命周期。
- `PipelineMode`：解析 `--pipeline` 和 `--frames`，生成明确错误信息。
- `TriangleData`：保存两条管线共用的 Position、Color 和三角形索引。
- `Common.hlsli`：共享常量结构和 Shader 输入输出定义。
- `VertexPipeline.hlsl`：只提供传统管线的 `VSMain`。
- `MeshPipeline.hlsl`：只提供 Mesh Shader 管线的 `MSMain`。
- `PixelShader.hlsl`：提供两条管线共同使用并只编译一次的 `PSMain`，避免“逻辑等价但二进制不同”的额外变量。
- `Smoke.Tests.ps1`：执行构建产物的参数与有限帧运行验证。

## 技术栈与构建约束

- C++17。
- Visual Studio 2022、x64。
- Windows SDK 10.0.26100.0。
- Win32、DXGI 1.6、Direct3D 12。
- DXC 在构建阶段编译 HLSL：
  - Vertex Shader：`vs_6_0`；
  - 公共 Pixel Shader：`ps_6_0`；
  - Mesh Shader：`ms_6_5`。
- Shader 编译产物输出到可执行文件旁的 `shaders` 目录，运行时按可执行文件路径加载。
- Debug 构建在系统提供 D3D12 Debug Layer 时启用；缺少可选 Graphics Tools 组件时记录警告并继续。Release 构建不启用 Debug Layer。

## 公共场景

场景只包含一个持续旋转的彩色三角形：

```text
Vertex 0：Position + Color
Vertex 1：Position + Color
Vertex 2：Position + Color
Index：0、1、2
```

CPU 每帧更新一个小型常量缓冲，提供二维旋转或等价的变换参数。两条管线产生相同的 Clip-Space Position 和插值颜色。

不使用 Depth Buffer。Rasterizer State 使用 `D3D12_CULL_MODE_NONE`，使示例不把 Front Face 约定引入两条几何入口的比较。

## Vertex Shader 路径

Vertex 模式创建传统 Graphics PSO，并执行：

```text
TriangleData
→ Default Heap Vertex Buffer / Index Buffer
→ D3D12_VERTEX_BUFFER_VIEW / D3D12_INDEX_BUFFER_VIEW
→ Input Assembler
→ DrawIndexedInstanced(3, 1, 0, 0, 0)
→ VSMain，每个 Invocation 处理一个逻辑 Vertex
→ 固定拓扑根据 Index 形成一个 Triangle
```

Vertex Shader 读取 IA 提供的 Position 与 Color，应用公共旋转变换，输出 `SV_Position` 与 Color。

## Mesh Shader 路径

Mesh 模式创建 Mesh Shader Graphics PSO，并执行：

```text
TriangleData
→ Default Heap Buffer 的 SRV
→ DispatchMesh(1, 1, 1)
→ 一个 Mesh Shader Thread Group
→ Shader 显式读取 Position、Color 和 Index
→ SetMeshOutputCounts(3, 1)
→ 输出 3 个 Vertex 和 1 个 Primitive
```

`MSMain` 使用 `[numthreads(32, 1, 1)]`，在 NVIDIA GPU 上对应一个 Warp：

- Thread 0～2 读取并处理三个 Vertex；
- Thread 0 写入三角形 Primitive Index；
- `SetMeshOutputCounts(3, 1)` 在一致控制流中调用，并支配所有 Mesh Output 写入；
- Group 通过少量 `groupshared` 数据和一次 Group Barrier 展示稳定协作范围，全部 32 个 Thread 都到达 Barrier；
- 其余 Thread 保持非活跃工作分支，但仍属于该 Group。

Shared Memory 的使用以教学可见性为目的，不作为这个三顶点工作负载的性能优化结论。

## 两条路径的汇合点

比较范围止于完整图元形成：

```text
Vertex 路径：
VB / IB → IA → VS → Vertex Output + 固定拓扑形成 Primitive

Mesh 路径：
Buffer SRV → MS Thread Group → Vertex Output + Primitive Index Output

汇合：
完整 Triangle Primitive
→ Clipping
→ Viewport
→ Primitive / Attribute Setup
→ Rasterization
→ Pixel Shader
→ Render Target
```

两种模式绑定同一个 `PixelShader.cso`，只输出插值后的顶点颜色。示例不通过 Pixel Shader 增加任何管线差异。

## D3D12 接口边界

传统管线使用常规接口：

- `D3D12_GRAPHICS_PIPELINE_STATE_DESC`；
- `ID3D12Device::CreateGraphicsPipelineState`；
- `IASetVertexBuffers`、`IASetIndexBuffer` 和 `DrawIndexedInstanced`。

Mesh Shader 管线使用 Mesh Shader 对应接口：

- `D3D12_PIPELINE_STATE_STREAM_DESC`，包含 `D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS` 和公共 Pixel Shader；
- `ID3D12Device2::CreatePipelineState`；
- `ID3D12GraphicsCommandList6::DispatchMesh`。

两种模式使用一份兼容的 Graphics Root Signature：

- Root CBV `b0`：公共旋转常量；
- Root SRV `t0`：Vertex Buffer 的 GPU Virtual Address，只由 Mesh Shader 使用；
- Root SRV `t1`：Index Buffer 的 GPU Virtual Address，只由 Mesh Shader 使用；
- Root Signature 保留 `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`，供 Vertex 模式使用。

SRV 使用 Root Descriptor，不创建额外的 CBV/SRV/UAV Descriptor Heap。Mesh Shader 使用 `StructuredBuffer` 读取数据，结构步长由 HLSL 类型定义。

## D3D12 资源与状态

- 使用两个 Default Heap Buffer 分别保存 Vertex 与 Index 数据；两种模式复用同一份 GPU 资源，不为 Mesh 模式复制第二份几何数据。
- 初始化阶段通过 Upload Heap 和 Copy Command 上传数据。
- Vertex Buffer 进入组合只读状态 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`。
- Index Buffer 进入组合只读状态 `D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`。
- Vertex 模式通过 VBV/IBV 读取资源；Mesh 模式通过 Root SRV 显式读取相同资源。
- 常量缓冲按 D3D12 的 256 字节对齐要求分配。
- 使用双缓冲 SwapChain、RTV Descriptor Heap、每帧 Command Allocator 和 Fence Value。
- 为保持示例可读性，允许每帧使用简单 Fence 同步，不以最大化 CPU/GPU 并行为目标。

## Adapter 与 Mesh Shader 能力

程序优先选择非软件的高性能 DXGI Adapter，并创建 D3D12 Device。Mesh 模式在创建 PSO 前查询 `D3D12_FEATURE_D3D12_OPTIONS7::MeshShaderTier`。

- Vertex 模式不要求 Mesh Shader 支持。
- Mesh 模式检测到 `D3D12_MESH_SHADER_TIER_NOT_SUPPORTED` 时返回明确错误。
- Mesh 模式不静默切换到 Vertex 模式，避免掩盖实际执行路径。

## 错误处理

- 无效或缺失的 `--pipeline` 参数：输出用法并以非零状态退出。
- 无效 `--frames`：输出参数错误并退出。
- DXGI、D3D12、Shader 加载或 PSO 创建失败：保留 HRESULT，显示包含阶段信息的错误。
- Debug 构建尝试启用 D3D12 Debug Layer；系统未安装 Graphics Tools 时记录警告并继续运行，不把可选调试组件作为启动前提。
- 工程使用 Windows Subsystem。主线程捕获初始化与帧循环异常，通过 `OutputDebugString` 和 MessageBox 展示，并返回非零进程退出码。

## 验证策略

### 参数测试

`Smoke.Tests.ps1` 以有限帧模式验证：

- `--pipeline vertex --frames 1` 被接受并正常退出；
- `--pipeline mesh --frames 1` 被接受并正常退出；
- 未知管线名称返回非零退出码；
- 非法 `--frames` 返回非零退出码。

测试脚本不会以无限帧模式启动应用，避免自动验证挂起。

### 构建验证

- 使用 VS2022 MSBuild 构建 `Debug|x64`；
- 使用 VS2022 MSBuild 构建 `Release|x64`；
- 构建过程同时验证三个 HLSL Profile 能由 Windows SDK DXC 编译。

### 运行验证

在 RTX 4080 SUPER 上分别执行：

```powershell
my-engine-demo.exe --pipeline vertex --frames 3
my-engine-demo.exe --pipeline mesh --frames 3
```

验收条件：

- 两个命令均以退出码 0 结束；
- Debug Layer 不报告资源状态、Root Signature、PSO 或命令列表错误；
- Vertex 模式实际提交 `DrawIndexedInstanced`；
- Mesh 模式实际提交 `DispatchMesh`；
- 无限帧模式下两者均显示持续旋转、颜色一致的三角形。

## 文档

README 包含：

- 环境要求；
- VS2022 打开、构建和运行步骤；
- 两个启动命令；
- 两条几何数据路径对照；
- Thread Group、Warp、Register、Groupshared 与 SM 的关系；
- Mesh Shader 能力检查和错误说明；
- 本示例刻意排除的材质、Texture 与光照范围。

## 验收条件

- `my-engine-demo.sln` 可由本机 VS2022 打开。
- `Debug|x64` 与 `Release|x64` 均构建成功。
- 生成单一 `my-engine-demo.exe`。
- `--pipeline vertex` 使用 `DrawIndexedInstanced`。
- `--pipeline mesh` 使用 `DispatchMesh`。
- 两种模式渲染相同旋转三角形并共享固定功能后端。
- 无第三方依赖。
- 自动有限帧验证能够在本机完成。
