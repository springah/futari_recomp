// futari_recomp (2026-11-15): the game's gameplay blit redrawn by the host. The game's blit draws the gameplay
// texture into the frame with one pixel shader (rgb = slot 0's texel x pixel constant c2.rgb, alpha = c2.w, then the
// alpha test); a draw futari_screen picks as that blit takes one of these instead (pipeline.cpp, Build):
// - GAMEPLAY_BLIT_SHARP 1 (sharp_bilinear_ps, Sharp bilinear): per source axis the texture is prescaled by
//   k = max(1, floor(scale)) nearest, then filtered bilinear, done in one step by moving the texel coordinate
//   (src/sharp_bilinear_math.h, SharpK and SharpTexel; keep the two the same).
// - GAMEPLAY_BLIT_SHARP 0 (blit_bilinear_ps, On): k = 1, i.e. plain bilinear.
// The bilinear is done by hand with four Loads and float weights (clamp addressing), not by the sampler, whose
// weights are coarser.
//
// Inputs and resources are the recompiled pixel shader's (XenosRecomp, RENUT_RECOMP): the full input list in its
// order (DXIL packs the signature and SPIR-V numbers the locations by declaration order), and the same bindings.

#ifdef __spirv__
// The guest shaders' uniform buffers (shader_common.h): pixel constants in set 6, SharedConstants in set 7.
struct PixelConstants { float4 v[256]; };
struct SharedConstants { uint4 v[85]; };
[[vk::binding(0, 6)]] ConstantBuffer<PixelConstants> g_Pixel;
[[vk::binding(0, 7)]] ConstantBuffer<SharedConstants> g_Shared;

float4 MaterialColour() { return g_Pixel.v[2]; }
uint TextureIndex() { return g_Shared.v[0].x; }          // byte 0: fetch slot 0's 2D texture descriptor
float AlphaThreshold() { return asfloat(g_Shared.v[34].z); }  // byte 552
uint AlphaTestFunc() { return g_Shared.v[34].w; }        // byte 556
#else
cbuffer PixelShaderConstants : register(b1, space4)
{
    float4 g_C[256] : packoffset(c0);
};
cbuffer SharedConstants : register(b2, space4)
{
    uint g_TextureIndex0 : packoffset(c0.x);
    float g_AlphaThreshold : packoffset(c34.z);
    uint g_AlphaTestFunc : packoffset(c34.w);
};

float4 MaterialColour() { return g_C[2]; }
uint TextureIndex() { return g_TextureIndex0; }
float AlphaThreshold() { return g_AlphaThreshold; }
uint AlphaTestFunc() { return g_AlphaTestFunc; }
#endif

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);

// Bits 26-31 of a descriptor index are flags (stacked texture, resolution scale code).
static const uint kDescriptorIndexMask = 0x03FFFFFFu;
// A degenerate derivative gives an infinite scale; k stays finite (the band 0.5/k is then far below a texel).
// The same value as kMaxScale in src/sharp_bilinear_math.h (SharpK); keep the two the same.
static const float kMaxScale = 4096.0;

// SharpTexel of sharp_bilinear_math.h, per axis.
float2 SharpTexel(float2 t, float2 k)
{
    const float2 cell = floor(t);
    const float2 c = t - cell - 0.5;
    const float2 r = 0.5 - 0.5 / k;
    return cell + (c - clamp(c, -r, r)) * k + 0.5;
}

// The guest's alpha test (shader_common.h, alphaTest): 0 never ... 7 always.
void AlphaTest(float alpha, uint func, float reference)
{
    const bool pass = select(func == 0u, false,
                      select(func == 1u, alpha < reference,
                      select(func == 2u, alpha == reference,
                      select(func == 3u, alpha <= reference,
                      select(func == 4u, alpha > reference,
                      select(func == 5u, alpha != reference,
                      select(func == 6u, alpha >= reference, true)))))));
    if (!pass)
        discard;
}

void main(
    in float4 iPos : SV_Position,
    in float4 iTexCoord0 : TEXCOORD0,
    in float4 iTexCoord1 : TEXCOORD1,
    in float4 iTexCoord2 : TEXCOORD2,
    in float4 iTexCoord3 : TEXCOORD3,
    in float4 iTexCoord4 : TEXCOORD4,
    in float4 iTexCoord5 : TEXCOORD5,
    in float4 iTexCoord6 : TEXCOORD6,
    in float4 iTexCoord7 : TEXCOORD7,
    in float4 iTexCoord8 : TEXCOORD8,
    in float4 iTexCoord9 : TEXCOORD9,
    in float4 iTexCoord10 : TEXCOORD10,
    in float4 iTexCoord11 : TEXCOORD11,
    in float4 iTexCoord12 : TEXCOORD12,
    in float4 iTexCoord13 : TEXCOORD13,
    in float4 iTexCoord14 : TEXCOORD14,
    in float4 iTexCoord15 : TEXCOORD15,
    in float4 iColor0 : COLOR0,
    in float4 iColor1 : COLOR1,
    in float4 iNormal0 : NORMAL0,
    in float4 iNormal1 : NORMAL1,
    in float4 iNormal2 : NORMAL2,
    in float4 iTangent0 : TANGENT0,
    in float4 iBinormal0 : BINORMAL0,
    in float2 iPointCoord : POINTCOORD,
#ifdef __spirv__
    in bool iFace : SV_IsFrontFace,
#else
    in uint iFace : SV_IsFrontFace,
#endif
    out float4 oC0 : SV_Target0)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[TextureIndex() & kDescriptorIndexMask];
    uint2 size;
    texture.GetDimensions(size.x, size.y);

    const float2 t = iTexCoord0.xy * float2(size);
#if GAMEPLAY_BLIT_SHARP
    // Per source axis, screen pixels per texel: taken first, before anything discards, so every pixel of the quad
    // still has its neighbours. Per source axis, so a rotated (tate) screen gets each axis' own.
    const float2 dx = ddx(t);
    const float2 dy = ddy(t);
    const float2 scale = min(1.0 / float2(length(float2(dx.x, dy.x)), length(float2(dx.y, dy.y))), kMaxScale);
    const float2 k = max(1.0, floor(scale + 1e-3));
#else
    const float2 k = 1.0;
#endif

    // Bilinear at the moved coordinate (texel centres at i + 0.5), clamped to the edge texels.
    const float2 s = SharpTexel(t, k) - 0.5;
    const float2 s0 = floor(s);
    const float2 f = s - s0;
    const int2 last = int2(size) - 1;
    const int2 i0 = clamp(int2(s0), 0, last);
    const int2 i1 = clamp(int2(s0) + 1, 0, last);
    const float3 c00 = texture.Load(int3(i0.x, i0.y, 0)).rgb;
    const float3 c10 = texture.Load(int3(i1.x, i0.y, 0)).rgb;
    const float3 c01 = texture.Load(int3(i0.x, i1.y, 0)).rgb;
    const float3 c11 = texture.Load(int3(i1.x, i1.y, 0)).rgb;
    const float3 top = c00 + (c10 - c00) * f.x;
    const float3 bottom = c01 + (c11 - c01) * f.x;
    const float3 colour = top + (bottom - top) * f.y;

    const float4 material = MaterialColour();
    oC0 = float4(colour * material.rgb, material.w);
    AlphaTest(oC0.w, AlphaTestFunc(), AlphaThreshold());
}
