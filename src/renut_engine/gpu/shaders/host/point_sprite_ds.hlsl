// futari_recomp (2026-11-04, leg/rectlist): see point_sprite_hs.hlsl. The
// corner at domain (u, v) is point_sprite_gs.hlsl's corner (2u - 1, 1 - 2v)
// with the point coordinate (u, v): its strip's four corners in its order.
// The size and the SharedConstants it reads are the geometry shader's; the
// draw's push constants reach this stage (pipeline.cpp adds it to the range
// when this form is in use).

#include "rectangle_list.hlsli"

#if defined(__spirv__)
struct PushConstants
{
    uint64_t VertexShaderConstants;
    uint64_t PixelShaderConstants;
    uint64_t SharedConstants;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

float4 PointState() { return vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 1312, 16); }
float4 ViewportState() { return vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 1328, 16); }

// The vertex shaders' clip space has y flipped (-fvk-invert-y).
static const float kUp = -1.0;
#else
cbuffer SharedConstants : register(b2, space4)
{
    float4 g_PointState : packoffset(c82);
    float4 g_ViewportState : packoffset(c83);
};

float4 PointState() { return g_PointState; }
float4 ViewportState() { return g_ViewportState; }

static const float kUp = 1.0;
#endif

[domain("quad")]
Vertex main(RectanglePatch patch, float2 uv : SV_DomainLocation, const OutputPatch<Vertex, 1> points)
{
    const Vertex v = points[0];
    const float4 state = PointState();
    float2 size = v.pointSize > 0.0 ? v.pointSize.xx : state.xy;
    if (state.w > 0.0)
        size = min(size, state.w);
    size = max(size, state.z);

    // Half the size in clip space: a pixel is 2 / viewport of the NDC range.
    const float2 half_size = size / ViewportState().xy * v.position.w;
    const float2 corner = float2(uv.x > 0.5 ? 1.0 : -1.0, uv.y > 0.5 ? -1.0 : 1.0);
    Vertex output = v;
    output.position.xy += corner * float2(1.0, kUp) * half_size;
    output.pointCoord = float2(uv.x > 0.5 ? 1.0 : 0.0, uv.y > 0.5 ? 1.0 : 0.0);
    return output;
}
