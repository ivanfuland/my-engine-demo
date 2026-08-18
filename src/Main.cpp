#include "PipelineMode.h"
#include "D3D12App.h"

#include <Windows.h>
#include <shellapi.h>

#include <stdexcept>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t ClientWidth = 720;
constexpr std::uint32_t ClientHeight = 720;
constexpr wchar_t WindowClassName[] = L"MyEngineDemoWindowClass";

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::wstring WidenMessage(const char* message) {
    std::wstring result;
    while (*message != '\0') {
        result.push_back(static_cast<unsigned char>(*message));
        ++message;
    }
    return result;
}

HWND CreateApplicationWindow(HINSTANCE instance, PipelineMode mode, int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    RECT windowRectangle{0, 0, static_cast<LONG>(ClientWidth), static_cast<LONG>(ClientHeight)};
    const DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (!AdjustWindowRect(&windowRectangle, windowStyle, FALSE)) {
        throw std::runtime_error("AdjustWindowRect failed");
    }

    const wchar_t* title = mode == PipelineMode::Vertex
                               ? L"DX12 Pipeline Comparison - Vertex Shader"
                               : L"DX12 Pipeline Comparison - Mesh Shader";
    HWND window = CreateWindowExW(
        0, WindowClassName, title, windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRectangle.right - windowRectangle.left, windowRectangle.bottom - windowRectangle.top,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    return window;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argumentCount = 0;
    wchar_t** argumentValues = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (argumentValues == nullptr) {
        OutputDebugStringW(L"CommandLineToArgvW failed.\n");
        return 2;
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(argumentCount > 1 ? static_cast<std::size_t>(argumentCount - 1) : 0);
    for (int index = 1; index < argumentCount; ++index) {
        arguments.emplace_back(argumentValues[index]);
    }
    LocalFree(argumentValues);

    AppOptions options{};
    try {
        options = ParseOptions(arguments);
    } catch (const std::invalid_argument& error) {
        std::wstring message(error.what(), error.what() + std::char_traits<char>::length(error.what()));
        message += L"\n";
        message += BuildUsageText();
        OutputDebugStringW(message.c_str());
        return 2;
    }

    HWND window = nullptr;
    try {
        window = CreateApplicationWindow(instance, options.pipeline, showCommand);
        D3D12App application(window, ClientWidth, ClientHeight, options.pipeline);
        application.Initialize();

        const auto startTime = std::chrono::steady_clock::now();
        std::uint64_t renderedFrames = 0;
        bool running = true;
        while (running) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) {
                break;
            }

            const float elapsedSeconds =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
            application.RenderFrame(elapsedSeconds);
            ++renderedFrames;
            if (options.frameLimit.has_value() && renderedFrames >= *options.frameLimit) {
                application.WaitForGpu();
                if (application.HasDebugValidationErrors()) {
                    DestroyWindow(window);
                    return 6;
                }
                running = false;
            }
        }
        application.WaitForGpu();
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        return 0;
    } catch (const MeshShaderUnsupported& error) {
        std::wstring message = WidenMessage(error.what());
        message += L"\n";
        OutputDebugStringW(message.c_str());
        if (!options.frameLimit.has_value()) {
            MessageBoxW(window, message.c_str(), L"my-engine-demo error", MB_OK | MB_ICONERROR);
        }
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        return 5;
    } catch (const std::exception& error) {
        std::wstring message = WidenMessage(error.what());
        message += L"\n";
        OutputDebugStringW(message.c_str());
        if (!options.frameLimit.has_value()) {
            MessageBoxW(window, message.c_str(), L"my-engine-demo error", MB_OK | MB_ICONERROR);
        }
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        return 4;
    }
}
