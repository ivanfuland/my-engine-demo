#include "Common.hlsli"

struct VSInput
{
    float2 position : POSITION;
    float3 color : COLOR0;
};

RasterVertex VSMain(VSInput input)
{
    RasterVertex output;
    output.position = float4(RotatePosition(input.position), 0.0f, 1.0f);
    output.color = input.color;
    return output;
}

