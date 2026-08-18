#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class PipelineMode {
    Vertex,
    Mesh,
};

struct AppOptions {
    PipelineMode pipeline;
    std::optional<std::uint64_t> frameLimit;
};

AppOptions ParseOptions(const std::vector<std::wstring>& args);
std::wstring BuildUsageText();

