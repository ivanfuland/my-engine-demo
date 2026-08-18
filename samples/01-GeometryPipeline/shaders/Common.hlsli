struct VertexData
{
    float2 position;
    float3 color;
};

struct RasterVertex
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

cbuffer SceneConstants : register(b0)
{
    float2 rotation;
};

float2 RotatePosition(float2 position)
{
    return float2(
        position.x * rotation.x - position.y * rotation.y,
        position.x * rotation.y + position.y * rotation.x);
}

