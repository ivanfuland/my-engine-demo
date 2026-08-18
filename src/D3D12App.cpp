#include "D3D12App.h"

#include "TriangleData.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

void ThrowIfFailed(HRESULT result, const char* stage) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string(stage) + " failed with HRESULT 0x" +
                                 std::to_string(static_cast<unsigned long>(result)));
    }
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t size) {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Alignment = 0;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    return description;
}

D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Unable to open shader: " + path.string());
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        throw std::runtime_error("Shader is empty: " + path.string());
    }

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Unable to read shader: " + path.string());
    }
    return data;
}

D3D12_BLEND_DESC DefaultBlendDescription() {
    D3D12_BLEND_DESC description{};
    description.AlphaToCoverageEnable = FALSE;
    description.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC target{
        FALSE,
        FALSE,
        D3D12_BLEND_ONE,
        D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE,
        D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (auto& renderTarget : description.RenderTarget) {
        renderTarget = target;
    }
    return description;
}

D3D12_RASTERIZER_DESC DefaultRasterizerDescription() {
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = D3D12_FILL_MODE_SOLID;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.FrontCounterClockwise = FALSE;
    description.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.DepthClipEnable = TRUE;
    description.MultisampleEnable = FALSE;
    description.AntialiasedLineEnable = FALSE;
    description.ForcedSampleCount = 0;
    description.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return description;
}

D3D12_DEPTH_STENCIL_DESC DisabledDepthStencilDescription() {
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = FALSE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    description.StencilEnable = FALSE;
    description.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    description.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    description.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                             D3D12_COMPARISON_FUNC_ALWAYS};
    description.BackFace = description.FrontFace;
    return description;
}

} // namespace

std::filesystem::path GetExecutableDirectory() {
    std::wstring pathBuffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (length == 0 || length == pathBuffer.size()) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetModuleFileNameW");
    }
    pathBuffer.resize(length);
    return std::filesystem::path(pathBuffer).parent_path();
}

D3D12App::D3D12App(HWND hwnd, std::uint32_t width, std::uint32_t height, PipelineMode mode)
    : hwnd_(hwnd), width_(width), height_(height), mode_(mode) {}

D3D12App::~D3D12App() {
    if (commandQueue_ && fence_ && fenceEvent_ != nullptr) {
        try {
            WaitForGpu();
        } catch (...) {
            OutputDebugStringW(L"WaitForGpu failed during shutdown.\n");
        }
    }
    if (constantBuffer_ && mappedConstants_ != nullptr) {
        constantBuffer_->Unmap(0, nullptr);
        mappedConstants_ = nullptr;
    }
    if (fenceEvent_ != nullptr) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void D3D12App::Initialize() {
    CreateDeviceAndSwapChain();
    CreateFrameResources();
    CreateRootSignature();
    CreateConstantBuffer();
    UploadGeometry();
    CreateVertexPipeline();
}

void D3D12App::CreateDeviceAndSwapChain() {
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugLayerEnabled_ = true;
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    } else {
        OutputDebugStringW(L"D3D12 Debug Layer is unavailable; continuing without it.\n");
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter1> selectedAdapter;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumerationResult = factory_->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (enumerationResult == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        ThrowIfFailed(enumerationResult, "EnumAdapterByGpuPreference");

        DXGI_ADAPTER_DESC1 description{};
        ThrowIfFailed(candidate->GetDesc1(&description), "IDXGIAdapter1::GetDesc1");
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
            selectedAdapter = candidate;
            break;
        }
    }

    if (!selectedAdapter) {
        throw std::runtime_error("No hardware adapter supports D3D feature level 12_0");
    }
    ThrowIfFailed(D3D12CreateDevice(selectedAdapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                    IID_PPV_ARGS(&device_)),
                  "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDescription.NodeMask = 0;
    ThrowIfFailed(device_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&commandQueue_)),
                  "CreateCommandQueue");

    DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
    swapChainDescription.Width = width_;
    swapChainDescription.Height = height_;
    swapChainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDescription.Stereo = FALSE;
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.BufferCount = FrameCount;
    swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDescription.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(commandQueue_.Get(), hwnd_, &swapChainDescription,
                                                   nullptr, nullptr, &swapChain),
                  "CreateSwapChainForHwnd");
    ThrowIfFailed(factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER),
                  "MakeWindowAssociation");
    ThrowIfFailed(swapChain.As(&swapChain_), "Query IDXGISwapChain3");
}

void D3D12App::CreateFrameResources() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDescription.NumDescriptors = FrameCount;
    rtvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(&rtvHeap_)),
                  "Create RTV descriptor heap");
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t index = 0; index < FrameCount; ++index) {
        ThrowIfFailed(swapChain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index])),
                      "Get swap chain buffer");
        device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize_;
        ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS(&frames_[index].commandAllocator)),
                      "CreateCommandAllocator");
    }

    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             frames_[0].commandAllocator.Get(), nullptr,
                                             IID_PPV_ARGS(&commandList_)),
                  "CreateCommandList");
    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                  "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    scissorRect_ = {0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
}

void D3D12App::CreateRootSignature() {
    std::array<D3D12_ROOT_PARAMETER1, 3> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[2].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
    description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    description.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
    description.Desc_1_1.pParameters = parameters.data();
    description.Desc_1_1.NumStaticSamplers = 0;
    description.Desc_1_1.pStaticSamplers = nullptr;
    description.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedSignature;
    ComPtr<ID3DBlob> errorBlob;
    const HRESULT serializationResult = D3D12SerializeVersionedRootSignature(
        &description, &serializedSignature, &errorBlob);
    if (FAILED(serializationResult) && errorBlob) {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    ThrowIfFailed(serializationResult, "D3D12SerializeVersionedRootSignature");
    ThrowIfFailed(device_->CreateRootSignature(0, serializedSignature->GetBufferPointer(),
                                               serializedSignature->GetBufferSize(),
                                               IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature");
}

void D3D12App::CreateConstantBuffer() {
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC resourceDescription =
        BufferDescription(ConstantBufferSliceSize * FrameCount);
    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescription,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer_)),
                  "Create constant buffer");
    D3D12_RANGE readRange{0, 0};
    ThrowIfFailed(constantBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedConstants_)),
                  "Map constant buffer");
}

void D3D12App::UploadGeometry() {
    const auto& vertices = TriangleVertices();
    const auto& indices = TriangleIndices();
    const std::uint64_t vertexSize = sizeof(vertices);
    const std::uint64_t indexSize = sizeof(indices);

    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexDescription = BufferDescription(vertexSize);
    const D3D12_RESOURCE_DESC indexDescription = BufferDescription(indexSize);

    ThrowIfFailed(device_->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDescription,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer_)),
                  "Create vertex buffer");
    ThrowIfFailed(device_->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &indexDescription,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&indexBuffer_)),
                  "Create index buffer");

    ComPtr<ID3D12Resource> vertexUpload;
    ComPtr<ID3D12Resource> indexUpload;
    ThrowIfFailed(device_->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDescription,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexUpload)),
                  "Create vertex upload buffer");
    ThrowIfFailed(device_->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDescription,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexUpload)),
                  "Create index upload buffer");

    void* mappedVertexData = nullptr;
    D3D12_RANGE readRange{0, 0};
    ThrowIfFailed(vertexUpload->Map(0, &readRange, &mappedVertexData), "Map vertex upload buffer");
    std::memcpy(mappedVertexData, vertices.data(), static_cast<std::size_t>(vertexSize));
    vertexUpload->Unmap(0, nullptr);

    void* mappedIndexData = nullptr;
    ThrowIfFailed(indexUpload->Map(0, &readRange, &mappedIndexData), "Map index upload buffer");
    std::memcpy(mappedIndexData, indices.data(), static_cast<std::size_t>(indexSize));
    indexUpload->Unmap(0, nullptr);

    commandList_->CopyBufferRegion(vertexBuffer_.Get(), 0, vertexUpload.Get(), 0, vertexSize);
    commandList_->CopyBufferRegion(indexBuffer_.Get(), 0, indexUpload.Get(), 0, indexSize);
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{
        TransitionBarrier(vertexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(indexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_INDEX_BUFFER |
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    ThrowIfFailed(commandList_->Close(), "Close upload command list");
    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    WaitForGpu();

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexSize);
    vertexBufferView_.StrideInBytes = sizeof(TriangleVertex);
    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexSize);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void D3D12App::CreateVertexPipeline() {
    const std::vector<std::byte> vertexShader =
        ReadBinaryFile(GetExecutableDirectory() / L"shaders" / L"VertexShader.cso");
    const std::vector<std::byte> pixelShader =
        ReadBinaryFile(GetExecutableDirectory() / L"shaders" / L"PixelShader.cso");

    const std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature_.Get();
    description.VS = {vertexShader.data(), vertexShader.size()};
    description.PS = {pixelShader.data(), pixelShader.size()};
    description.BlendState = DefaultBlendDescription();
    description.SampleMask = UINT_MAX;
    description.RasterizerState = DefaultRasterizerDescription();
    description.DepthStencilState = DisabledDepthStencilDescription();
    description.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
    description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.DSVFormat = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.NodeMask = 0;
    description.CachedPSO = {nullptr, 0};
    description.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&vertexPipeline_)),
                  "CreateGraphicsPipelineState");
}

void D3D12App::WaitForFrame(FrameContext& frame) {
    if (frame.fenceValue != 0 && fence_->GetCompletedValue() < frame.fenceValue) {
        ThrowIfFailed(fence_->SetEventOnCompletion(frame.fenceValue, fenceEvent_),
                      "SetEventOnCompletion for frame");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12App::RenderFrame(float elapsedSeconds) {
    const UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();
    FrameContext& frame = frames_[frameIndex];
    WaitForFrame(frame);

    ThrowIfFailed(frame.commandAllocator->Reset(), "Reset command allocator");
    ThrowIfFailed(commandList_->Reset(frame.commandAllocator.Get(), vertexPipeline_.Get()),
                  "Reset command list");

    SceneConstants constants{{std::cos(elapsedSeconds), std::sin(elapsedSeconds)}};
    std::memcpy(mappedConstants_ + frameIndex * ConstantBufferSliceSize, &constants, sizeof(constants));

    D3D12_RESOURCE_BARRIER toRenderTarget =
        TransitionBarrier(renderTargets_[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
                          D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * rtvDescriptorSize_;
    const float clearColor[] = {0.035f, 0.045f, 0.075f, 1.0f};
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetGraphicsRootConstantBufferView(
        0, constantBuffer_->GetGPUVirtualAddress() + frameIndex * ConstantBufferSliceSize);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->IASetIndexBuffer(&indexBufferView_);
    commandList_->DrawIndexedInstanced(3, 1, 0, 0, 0);

    D3D12_RESOURCE_BARRIER toPresent =
        TransitionBarrier(renderTargets_[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_PRESENT);
    commandList_->ResourceBarrier(1, &toPresent);
    ThrowIfFailed(commandList_->Close(), "Close frame command list");
    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    ThrowIfFailed(swapChain_->Present(1, 0), "Present");

    const std::uint64_t fenceValue = nextFenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue), "Signal frame fence");
    frame.fenceValue = fenceValue;
}

void D3D12App::WaitForGpu() {
    const std::uint64_t fenceValue = nextFenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue), "Signal GPU fence");
    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue, fenceEvent_),
                  "SetEventOnCompletion for GPU");
    WaitForSingleObject(fenceEvent_, INFINITE);
}

bool D3D12App::HasDebugValidationErrors() const {
#if defined(_DEBUG)
    if (!debugLayerEnabled_) {
        return false;
    }
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device_.As(&infoQueue))) {
        return false;
    }
    const std::uint64_t messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (std::uint64_t index = 0; index < messageCount; ++index) {
        SIZE_T messageLength = 0;
        if (FAILED(infoQueue->GetMessage(index, nullptr, &messageLength)) || messageLength == 0) {
            continue;
        }
        std::vector<std::byte> storage(messageLength);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue->GetMessage(index, message, &messageLength))) {
            continue;
        }
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
            OutputDebugStringA(message->pDescription);
            OutputDebugStringA("\n");
            return true;
        }
    }
#endif
    return false;
}

