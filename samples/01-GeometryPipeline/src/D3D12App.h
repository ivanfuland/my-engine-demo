#pragma once

#include "PipelineMode.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

class MeshShaderUnsupported : public std::runtime_error {
public:
    MeshShaderUnsupported() : std::runtime_error("Mesh Shader is not supported by this adapter") {}
};

class D3D12App {
public:
    D3D12App(HWND hwnd, std::uint32_t width, std::uint32_t height, PipelineMode mode);
    ~D3D12App();

    D3D12App(const D3D12App&) = delete;
    D3D12App& operator=(const D3D12App&) = delete;

    void Initialize();
    void RenderFrame(float elapsedSeconds);
    void WaitForGpu();
    bool HasDebugValidationErrors() const;

private:
    static constexpr std::uint32_t FrameCount = 2;
    static constexpr std::uint64_t ConstantBufferSliceSize = 256;

    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
        std::uint64_t fenceValue = 0;
    };

    struct SceneConstants {
        float rotation[2];
    };

    void CreateDeviceAndSwapChain();
    void CreateFrameResources();
    void CreateRootSignature();
    void CreateConstantBuffer();
    void UploadGeometry();
    void CreateVertexPipeline();
    void CheckMeshShaderSupport();
    void CreateMeshPipeline();
    void RecordMeshDraw();
    void WaitForFrame(FrameContext& frame);

    HWND hwnd_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    PipelineMode mode_ = PipelineMode::Vertex;
    bool debugLayerEnabled_ = false;

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12Device2> meshDevice_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    std::uint32_t rtvDescriptorSize_ = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> renderTargets_;
    std::array<FrameContext, FrameCount> frames_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> meshCommandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    std::uint64_t nextFenceValue_ = 1;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> vertexPipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> meshPipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    std::byte* mappedConstants_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
};

std::filesystem::path GetExecutableDirectory();
