# DX12 Sample Suite 目录重构设计

## 背景

当前仓库只有一个 Vertex Shader 与 Mesh Shader 对照程序，源码、Shader 和 VCXPROJ 均位于仓库根目录。后续计划增加 Ray Tracing、Bindless 等独立验证主题；继续把所有功能加入一个 `D3D12App` 会让 Root Signature、PSO、资源生命周期和能力检查相互耦合。

## 目标

- 将仓库从单 Demo 调整为可容纳多个独立 DX12 Sample 的 Solution。
- 保持当前 Vertex/Mesh 对照行为、参数契约和 VS2022 F5 调试能力。
- 每个 Sample 拥有独立项目、源码、Shader 和输出目录。
- 在根目录统一管理工具链配置、启动脚本、测试和总览文档。
- Debug/Release、Vertex/Mesh 在重构后继续构建和运行。

## 非目标

- 本次不实现 Ray Tracing 或 Bindless。
- 本次不抽取 `Dx12Common` 静态库。
- 本次不改变 Vertex/Mesh Shader 数据路径、Root Signature、资源状态或渲染结果。
- 不引入 CMake 或第三方依赖。

## 目录结构

```text
my-engine-demo/
├─ my-engine-demo.sln
├─ Directory.Build.props
├─ samples/
│  └─ 01-GeometryPipeline/
│     ├─ GeometryPipelineDemo.vcxproj
│     ├─ src/
│     └─ shaders/
├─ tests/
│  └─ Smoke.Tests.ps1
├─ run-vertex.cmd
├─ run-mesh.cmd
├─ README.md
└─ docs/
```

未来新增：

```text
samples/02-RayTracing/
samples/03-Bindless/
shared/Dx12Common/
```

`shared/Dx12Common` 只在第二个 Sample 出现后，根据真实重复代码提取，不在本次创建占位实现。

## 工程与输出

- 根 Solution 保留文件名 `my-engine-demo.sln`。
- 当前项目改名为 `GeometryPipelineDemo`，项目文件位于 `samples/01-GeometryPipeline/GeometryPipelineDemo.vcxproj`。
- `Directory.Build.props` 固定 Windows SDK `10.0.26100.0`、MSVC v143、C++17、公共中间目录与输出目录。
- 输出按项目隔离：

```text
x64/Debug/GeometryPipelineDemo/GeometryPipelineDemo.exe
x64/Debug/GeometryPipelineDemo/shaders/*.cso
x64/Release/GeometryPipelineDemo/GeometryPipelineDemo.exe
x64/Release/GeometryPipelineDemo/shaders/*.cso
```

- Shader Custom Build Item 的 include、输入和输出路径都相对 Sample 项目目录求值。
- VS 调试参数继续默认为 `--pipeline vertex`。

## 启动与测试

- `run-vertex.cmd`、`run-mesh.cmd` 继续保留在根目录，并指向新的独立输出目录。
- `tests/Smoke.Tests.ps1` 使用新的 EXE 路径，参数和退出码契约不变。
- 自动运行继续使用 `--frames N`，避免阻塞。

## 验收条件

- VS2022 可打开根 Solution，Solution 中包含 `GeometryPipelineDemo`。
- Debug|x64 与 Release|x64 均以0警告、0错误构建。
- 三个 Shader CSO 位于 GeometryPipelineDemo 的独立输出目录。
- Vertex/Mesh 的1帧与3帧 Smoke Test 均返回0。
- VS F5 默认启动 Vertex；项目调试参数可改为 Mesh。
- 两个根启动脚本带 `--frames 3` 运行返回0。
- 旧的根 `src/`、`shaders/` 和 `my-engine-demo.vcxproj` 不再存在。
- 构建产物不进入 Git。

