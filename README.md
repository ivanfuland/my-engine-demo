# DX12 Vertex Shader 与 Mesh Shader 管线对照

这个示例使用同一个 DX12 可执行程序渲染同一个旋转彩色三角形，只切换几何入口：

```text
my-engine-demo.exe --pipeline vertex
my-engine-demo.exe --pipeline mesh
```

- Vertex 模式通过 `DrawIndexedInstanced` 进入 Input Assembler 和 Vertex Shader。
- Mesh 模式通过 `DispatchMesh` 启动 Mesh Shader Thread Group，由 Shader 显式输出 Vertex 和 Primitive。
- 两条路径形成完整 Triangle 后，共用 Clipping、Viewport、Primitive Setup、Rasterizer、Pixel Shader 和 Render Target。

## 环境

- Windows 11
- Visual Studio 2022，Desktop development with C++
- MSVC v143
- Windows SDK `10.0.26100.0`
- 支持 Direct3D 12 Mesh Shader Tier 1 的 GPU；本项目验收设备为 NVIDIA RTX 4080 SUPER

工程不依赖 CMake、DirectXTK、`d3dx12.h` 或其他第三方库。

## 构建

使用 Visual Studio 2022 打开 `my-engine-demo.sln`，选择：

- `Debug | x64`，或
- `Release | x64`

也可以从 Developer PowerShell 构建：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\my-engine-demo.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64
```

构建同时调用 Windows SDK 中的 DXC，生成：

```text
x64\Debug\my-engine-demo.exe
x64\Debug\shaders\VertexShader.cso
x64\Debug\shaders\MeshShader.cso
x64\Debug\shaders\PixelShader.cso
```

两条管线加载的是同一个 `PixelShader.cso`。

## 运行

直接运行：

```powershell
.\x64\Debug\my-engine-demo.exe --pipeline vertex
.\x64\Debug\my-engine-demo.exe --pipeline mesh
```

也可以双击：

```text
run-vertex.cmd
run-mesh.cmd
```

脚本优先使用 Debug 产物，找不到时使用 Release 产物。程序默认持续运行到关闭窗口。自动验证使用有限帧参数：

```powershell
.\x64\Debug\my-engine-demo.exe --pipeline vertex --frames 3
.\x64\Debug\my-engine-demo.exe --pipeline mesh --frames 3

.\run-vertex.cmd --frames 3
.\run-mesh.cmd --frames 3
```

## 两条数据路径

### Vertex Shader

```text
TriangleData
→ Default Heap Vertex Buffer / Index Buffer
→ VBV / IBV
→ Input Assembler
→ DrawIndexedInstanced(3, 1, 0, 0, 0)
→ VSMain：每个 Invocation 处理一个逻辑 Vertex
→ 固定拓扑根据 Index 形成 Triangle
```

### Mesh Shader

```text
同一份 Default Heap Vertex Buffer / Index Buffer
→ Root SRV t0 / t1
→ DispatchMesh(1, 1, 1)
→ 一个 [numthreads(32, 1, 1)] Mesh Shader Thread Group
→ Thread 0～2 读取 Vertex，Thread 0 读取 Index
→ SetMeshOutputCounts(3, 1)
→ 输出 3 个 Vertex 和 1 个 Primitive
```

Vertex Buffer 的最终状态同时包含：

```text
VERTEX_AND_CONSTANT_BUFFER | NON_PIXEL_SHADER_RESOURCE
```

Index Buffer 的最终状态同时包含：

```text
INDEX_BUFFER | NON_PIXEL_SHADER_RESOURCE
```

因此传统 IA 与 Mesh Shader Root SRV 读取的是同一份 GPU 资源，并没有为 Mesh 模式复制第二份几何数据。

## Thread Group、Warp 与存储

- HLSL `Thread` 是一次逻辑 Shader Invocation，不对应一块固定的 CUDA Core。
- `Thread Group` 是程序声明的协作范围。这个示例声明32个 Thread，并通过 `groupshared` 数据和一次 Group Barrier 协作。
- NVIDIA Warp 是硬件调度和执行粒度。这里32个 Thread 通常组成一个 Warp；Warp 不是一个“组长硬件”。
- 每个 Thread 的临时值通常映射到 SM Register File 中分配给该 Thread 的寄存器状态。
- `groupshared` 是一个 Thread Group 共享的显式存储空间，物理上由承载该 Group 的 SM 提供。
- Warp 中的 Thread 会在执行过程中使用 CUDA Core、Load/Store、Texture 等不同硬件数据通路；Thread 不永久绑定某个固定计算核心。

这个三角形用 `groupshared` 只是为了显示 Thread Group 的稳定协作边界，不代表它比直接读取三个 Vertex 更快。

## 汇合点

```text
Vertex 路径：VB / IB → IA → VS → Vertex Output + 固定拓扑
Mesh 路径：  Buffer SRV → MS Group → Vertex Output + Primitive Index Output

汇合：完整 Triangle Primitive
→ Clipping
→ Viewport
→ Primitive / Attribute Setup
→ Rasterization
→ 公共 Pixel Shader
→ Render Target
```

## 示例边界

本示例不包含材质、Texture、Sampler、光照、Depth Buffer、相机、双 Viewport、Amplification Shader 或 Meshlet 构建。它们不影响这里要比较的几何入口差异。

Mesh 模式会先查询 `D3D12_FEATURE_D3D12_OPTIONS7::MeshShaderTier`。不支持时程序返回退出码5，不会回退到 Vertex 模式。

## Smoke Test

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\tests\Smoke.Tests.ps1 -Configuration Release
```

Debug 有限帧模式在退出前检查 D3D12 InfoQueue。出现 Error 或 Corruption 时返回退出码6。
