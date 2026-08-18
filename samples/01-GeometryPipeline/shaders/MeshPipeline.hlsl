#include "Common.hlsli"

StructuredBuffer<VertexData> vertexBuffer : register(t0);
StructuredBuffer<uint> indexBuffer : register(t1);

groupshared VertexData sharedVertices[3];

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MSMain(
    uint threadIndex : SV_GroupIndex,
    out vertices RasterVertex outputVertices[3],
    out indices uint3 primitiveIndices[1])
{
    SetMeshOutputCounts(3, 1);

    if (threadIndex < 3)
    {
        sharedVertices[threadIndex] = vertexBuffer[threadIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    if (threadIndex < 3)
    {
        VertexData vertex = sharedVertices[threadIndex];
        outputVertices[threadIndex].position =
            float4(RotatePosition(vertex.position), 0.0f, 1.0f);
        outputVertices[threadIndex].color = vertex.color;
    }

    if (threadIndex == 0)
    {
        primitiveIndices[0] = uint3(indexBuffer[0], indexBuffer[1], indexBuffer[2]);
    }
}

