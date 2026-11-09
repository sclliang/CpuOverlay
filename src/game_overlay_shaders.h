#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace GameOverlay {
inline constexpr char QuadShader[] = R"(
cbuffer Placement : register(b0) { float4 rect; };
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex VS(uint index : SV_VertexID) {
    Vertex v; v.uv = float2(index & 1, (index >> 1) & 1);
    v.position = float4(rect.xy + v.uv * rect.zw, 0, 1); return v;
}
Texture2D image : register(t0);
SamplerState sampleImage : register(s0);
float4 PS(Vertex v) : SV_TARGET { return image.Sample(sampleImage, v.uv); }
)";
inline HRESULT CompileQuad(Microsoft::WRL::ComPtr<ID3DBlob>& vertex, Microsoft::WRL::ComPtr<ID3DBlob>& pixel) {
    auto result = D3DCompile(QuadShader, sizeof(QuadShader) - 1, nullptr, nullptr, nullptr, "VS", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertex, nullptr);
    if (FAILED(result)) return result;
    return D3DCompile(QuadShader, sizeof(QuadShader) - 1, nullptr, nullptr, nullptr, "PS", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixel, nullptr);
}
}
