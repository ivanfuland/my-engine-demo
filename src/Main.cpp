#include "PipelineMode.h"

#include <Windows.h>
#include <shellapi.h>

#include <stdexcept>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
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

    try {
        static_cast<void>(ParseOptions(arguments));
    } catch (const std::invalid_argument& error) {
        std::wstring message(error.what(), error.what() + std::char_traits<char>::length(error.what()));
        message += L"\n";
        message += BuildUsageText();
        OutputDebugStringW(message.c_str());
        return 2;
    }

    return 3;
}
