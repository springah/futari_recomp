// futari_recomp (2026-11-04, leg/rectlist): see rectangle_list_hs.hlsl. The
// quad's corners at level 1 are exactly (0,0), (1,0), (0,1) and (1,1): the
// first three are the given corners c, a and b as they came, the last is
// a + b - c, the arithmetic of rectangle_list_gs.hlsl for every output.

#include "rectangle_list.hlsli"

[domain("quad")]
Vertex main(RectanglePatch patch, float2 uv : SV_DomainLocation, const OutputPatch<Vertex, 3> corners)
{
    const bool u = uv.x > 0.5;
    const bool v = uv.y > 0.5;
    if (u && v)
        return Opposite(corners[1], corners[2], corners[0]);
    if (u)
        return corners[1];
    if (v)
        return corners[2];
    return corners[0];
}
