// futari_recomp (2026-11-04, leg/rectlist): Xenos point sprites without a
// geometry shader, the tessellation form of point_sprite_gs.hlsl (Apple GPUs
// under MoltenVK). Each point is a patch of one control point, handed on as it
// is; one quad at level 1, or none (tessellation factors 0 discard the patch)
// for a vertex its shader killed (a negative size), as the geometry shader
// emits nothing for it. point_sprite_ds.hlsl places the four corners.

#include "rectangle_list.hlsli"

RectanglePatch Constants(InputPatch<Vertex, 1> input)
{
    const float level = input[0].pointSize < 0.0 ? 0.0 : 1.0;
    RectanglePatch patch;
    patch.edges[0] = level;
    patch.edges[1] = level;
    patch.edges[2] = level;
    patch.edges[3] = level;
    patch.inside[0] = level;
    patch.inside[1] = level;
    return patch;
}

// (0,0) -> (1,0) -> (0,1) is the geometry shader's first triangle (corners
// 0, 1, 2 of its strip), clockwise in the upper-left domain: the same winding.
[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(1)]
[patchconstantfunc("Constants")]
Vertex main(InputPatch<Vertex, 1> input, uint id : SV_OutputControlPointID)
{
    return input[0];
}
