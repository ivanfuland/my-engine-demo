#include "Common.hlsli"

float4 PSMain(RasterVertex input) : SV_Target0
{
    return float4(input.color, 1.0f);
}

