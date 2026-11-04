// futari_recomp (2026-11-04, leg/rectlist): what rectangle_list_hs.hlsl and
// rectangle_list_ds.hlsl share, the tessellation form of rectangle_list_gs.hlsl
// for devices without geometry shaders (Apple GPUs under MoltenVK). The vertex
// layout, the right-angle choice and the fourth corner are the geometry
// shader's, unchanged (see the comments there).

struct Vertex
{
    float4 position : SV_Position;
    float4 texcoord[16] : TEXCOORD0;
    float4 color[2] : COLOR0;
    float4 normal[3] : NORMAL0;
    float4 tangent : TANGENT0;
    float4 binormal : BINORMAL0;
    float2 pointCoord : POINTCOORD;
    float pointSize : POINTSIZE;
};

// A quad domain tessellated at level 1: four vertices, two triangles.
struct RectanglePatch
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

// a + b - c for every output: the corner across the diagonal a-b from c.
Vertex Opposite(Vertex a, Vertex b, Vertex c)
{
    Vertex d;
    d.position = a.position + b.position - c.position;
    [unroll] for (uint i = 0; i < 16; ++i)
        d.texcoord[i] = a.texcoord[i] + b.texcoord[i] - c.texcoord[i];
    [unroll] for (uint j = 0; j < 2; ++j)
        d.color[j] = a.color[j] + b.color[j] - c.color[j];
    [unroll] for (uint k = 0; k < 3; ++k)
        d.normal[k] = a.normal[k] + b.normal[k] - c.normal[k];
    d.tangent = a.tangent + b.tangent - c.tangent;
    d.binormal = a.binormal + b.binormal - c.binormal;
    d.pointCoord = a.pointCoord + b.pointCoord - c.pointCoord;
    d.pointSize = a.pointSize + b.pointSize - c.pointSize;
    return d;
}

float2 ScreenPosition(float4 position)
{
    return position.xy / position.w;
}

// How far the corner at p is from a right angle (see rectangle_list_gs.hlsl).
float Skew(float2 p, float2 a, float2 b)
{
    return abs(dot(a - p, b - p));
}

// The corner whose edges are perpendicular, chosen as the geometry shader does.
uint RightAngleCorner(float4 q0, float4 q1, float4 q2)
{
    const float2 p0 = ScreenPosition(q0);
    const float2 p1 = ScreenPosition(q1);
    const float2 p2 = ScreenPosition(q2);
    const float s0 = Skew(p0, p1, p2);
    const float s1 = Skew(p1, p2, p0);
    const float s2 = Skew(p2, p0, p1);
    uint corner = 0;
    if (s1 < s0 && s1 <= s2)
        corner = 1;
    else if (s2 < s0 && s2 < s1)
        corner = 2;
    return corner;
}
