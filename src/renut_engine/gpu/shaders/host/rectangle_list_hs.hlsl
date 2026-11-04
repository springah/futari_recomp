// futari_recomp (2026-11-04, leg/rectlist): Xenos rectangle lists without a
// geometry shader. The three corners of each rectangle are one patch; this
// hull shader hands them on rotated so that control point 0 is the right-angle
// corner c, 1 the next corner a and 2 the last b, in the given cyclic order
// (as rectangle_list_gs.hlsl starts its strip), and asks for one quad at
// level 1. rectangle_list_ds.hlsl places the four corners.

#include "rectangle_list.hlsli"

RectanglePatch Constants(InputPatch<Vertex, 3> input)
{
    RectanglePatch patch;
    patch.edges[0] = 1.0;
    patch.edges[1] = 1.0;
    patch.edges[2] = 1.0;
    patch.edges[3] = 1.0;
    patch.inside[0] = 1.0;
    patch.inside[1] = 1.0;
    return patch;
}

// Clockwise in the domain, whose origin is the upper left: (0,0) -> (1,0) ->
// (0,1) is c -> a -> b, so both triangles keep the given vertices' winding.
[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("Constants")]
Vertex main(InputPatch<Vertex, 3> input, uint id : SV_OutputControlPointID)
{
    const uint corner = RightAngleCorner(input[0].position, input[1].position, input[2].position);
    return input[(corner + id) % 3];
}
