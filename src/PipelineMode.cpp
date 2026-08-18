#include "PipelineMode.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>

namespace {

std::uint64_t ParsePositiveFrameCount(const std::wstring& value) {
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
        throw std::invalid_argument("--frames requires a positive integer");
    }

    std::size_t parsedCharacters = 0;
    unsigned long long parsedValue = 0;
    try {
        parsedValue = std::stoull(value, &parsedCharacters, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument("--frames is out of range");
    }

    if (parsedCharacters != value.size() || parsedValue == 0 ||
        parsedValue > (std::numeric_limits<std::uint64_t>::max)()) {
        throw std::invalid_argument("--frames requires a positive integer");
    }

    return static_cast<std::uint64_t>(parsedValue);
}

} // namespace

AppOptions ParseOptions(const std::vector<std::wstring>& args) {
    std::optional<PipelineMode> pipeline;
    std::optional<std::uint64_t> frameLimit;

    for (std::size_t index = 0; index < args.size();) {
        const std::wstring& argument = args[index];
        if (argument == L"--pipeline") {
            if (pipeline.has_value() || index + 1 >= args.size()) {
                throw std::invalid_argument("--pipeline is duplicated or missing a value");
            }

            const std::wstring& value = args[index + 1];
            if (value == L"vertex") {
                pipeline = PipelineMode::Vertex;
            } else if (value == L"mesh") {
                pipeline = PipelineMode::Mesh;
            } else {
                throw std::invalid_argument("--pipeline accepts vertex or mesh");
            }
            index += 2;
        } else if (argument == L"--frames") {
            if (frameLimit.has_value() || index + 1 >= args.size()) {
                throw std::invalid_argument("--frames is duplicated or missing a value");
            }
            frameLimit = ParsePositiveFrameCount(args[index + 1]);
            index += 2;
        } else {
            throw std::invalid_argument("unknown argument");
        }
    }

    if (!pipeline.has_value()) {
        throw std::invalid_argument("--pipeline is required");
    }

    return AppOptions{*pipeline, frameLimit};
}

std::wstring BuildUsageText() {
    return L"Usage: my-engine-demo.exe --pipeline vertex|mesh [--frames positive-integer]\n";
}

