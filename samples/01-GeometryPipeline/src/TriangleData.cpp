#include "TriangleData.h"

const std::array<TriangleVertex, 3>& TriangleVertices() {
    static const std::array<TriangleVertex, 3> vertices{{
        {{0.0f, 0.65f}, {1.0f, 0.15f, 0.10f}},
        {{0.60f, -0.45f}, {0.10f, 0.85f, 0.20f}},
        {{-0.60f, -0.45f}, {0.10f, 0.35f, 1.0f}},
    }};
    return vertices;
}

const std::array<std::uint32_t, 3>& TriangleIndices() {
    static const std::array<std::uint32_t, 3> indices{{0, 1, 2}};
    return indices;
}

