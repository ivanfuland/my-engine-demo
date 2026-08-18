#pragma once

#include <array>
#include <cstdint>

struct TriangleVertex {
    float position[2];
    float color[3];
};

const std::array<TriangleVertex, 3>& TriangleVertices();
const std::array<std::uint32_t, 3>& TriangleIndices();

