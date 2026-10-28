# Native GS sprite sampling convention — source-only follow-up

The native GS sprite raster uses **integer screen pixel centres**. Linear
texture sampling uses **half-integer texel centres**; point sampling selects
the integer part of interpolated UV. Our renderer evaluates unchanged GS XY
on D3D11's half-integer pixel centres. A native raster-origin conversion is
justified. A universal half-texel UV subtraction is not.

This strengthens the [orb atlas investigation](hud-orb-edge.md),
without changing renderer code or masking atlas pixels. Scale4 HUD behavior
still requires a defined enlarged-sprite policy and renderer regression.
No game, GPU or WARP execution occurred during this follow-up.

## Independent references

James Lee's [original GS hardware experiment](https://jbit.net/Pixel_Perfect_Sprites_on_the_PS2/)
explicitly distinguishes integer screen centres from half-integer texture
centres. Its hardware images also demonstrate exclusion of the bottom/right
endpoint. Its further endpoint recommendations address GS linear-filter/DDA
precision; they are not an emulator offset to apply to already-authored UVs.

PCSX2 was not found locally. Read-only upstream references are pinned to
commit `091ab68708a555198d80a7216965481c7ddd0d98`, privately saved under
`build/hud-orb-edge-20261018/references/`. No PCSX2 code was incorporated into
our renderer or arithmetic fixtures.

- [Software sprite rasterizer](https://github.com/PCSX2/pcsx2/blob/091ab68708a555198d80a7216965481c7ddd0d98/pcsx2/GS/Renderers/SW/GSRasterizer.cpp#L1088):
  rounds sprite endpoints up, then interpolates from the first integer pixel.
  Its sprite prestep adds no half pixel.
- [Software texture sampler](https://github.com/PCSX2/pcsx2/blob/091ab68708a555198d80a7216965481c7ddd0d98/pcsx2/GS/Renderers/SW/GSDrawScanline.cpp#L1076):
  selects point texels by shifting fixed-point coordinates. The half-texel
  subtraction belongs to linear filter tap construction.
- [Hardware raster setup](https://github.com/PCSX2/pcsx2/blob/091ab68708a555198d80a7216965481c7ddd0d98/pcsx2/GS/Renderers/HW/GSRendererHW.cpp#L6289):
  converts to host raster coordinates. Native-origin handling is separate
  from optional enlarged-sprite fixes.

Microsoft's [D3D11 specification, section3.3.1](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.3.1%20Pixel%20Coordinate%20System)
defines target origin at the upper-left corner and pixel centres at integer
coordinates plus(0.5,0.5). The top-left rule includes top/left rectangular
boundaries and excludes bottom/right boundaries.

Exact GS DDA quantization, nonconstant Q, AA lines/points and triangle edge
precision are outside this sprite-only conclusion. The fixtures use
representable coordinates and Q=1 to avoid those uncertainties.

## Our path and the correction it implies

In `runtime/gs/src/gs_core.cpp`, `kick()` subtracts XYOFFSET and converts
12.4 XY to floats. `toDV()` retains XY. `emitSprite()` expands two corners
into triangles with those XY/UV endpoints. STQ is normalized before expansion;
the captured highlight has Q=1 at both corners.

In `runtime/gs/src/gs_shaders.h`, `VSMain` converts unchanged XY to clip space.
Sprites receive no temporal jitter. The backend's viewport starts at(0,0),
so native attributes are evaluated at(x+0.5,y+0.5). `sampleLvl` itself uses the
expected texture convention: point floors UV, while linear forms taps around
UV-0.5. The discrepancy begins at the screen position used for interpolation.

For an increasing screen axis, after XYOFFSET:

```
GS covered pixel indices: ceil(X0) <= i < ceil(X1)
GS texcoord:              U0 + (i-X0) * (U1-U0)/(X1-X0)
Current native D3D11:     U0 + (i+0.5-X0) * (U1-U0)/(X1-X0)
```

At scale1, adding +0.5 to destination geometry maps D3D11 samples onto the
integer GS centres, including fractional coverage. This is an arithmetic
conclusion, not an instruction to edit `VSMain` globally: world, depth,
scissor, HUD transforms and auxiliary passes need coordinated treatment.
No production hunk was applied.

For already-covered native pixels, the equivalent affine UV correction is
`-0.5*dU/dX`, and vertically `-0.5*dV/dY`. Its sign reverses with the mapping,
its magnitude changes with texel density, and it is zero for constant UV.
A UV-only adjustment also leaves fractional coverage wrong.

| Sprite | Native GS point texels | Current native D3D11 | Blanket UV-0.5 |
| --- | --- | --- | --- |
| X0..4, U0.5..4.5 | 0,1,2,3 | 1,2,3,4 | Repairs this mapping |
| X0..4, constant U1 | 1,1,1,1 | 1,1,1,1 | Corrupts to0,0,0,0 |
| X0..4, U4.5..0.5 | 4,3,2,1 | 4,3,2,1 | Corrupts to3,2,1,0 |
| X0..4, U0.5..8.5 | 0,2,4,6 | 1,3,5,7 | Still1,3,5,7 |

Some reversed point results coincide because of quantization; their
interpolated coordinates still differ, which linear filtering exposes.
Deliberately authored filter weights and edge texels must be preserved.

## Orb interpretation and scale4

The native integer-centre rule gives the captured highlight u28.5..87.5,
v60.5..3.5: selected columns28..87 and rows3..60. None of the disc atlas's
border at x27/88 or y2/61 is sampled. This supplies an expected native
footprint independently of whether that artwork looks desirable.
An intentionally included border remains visible if the submitted GS samples
actually reach it; no atlas-content suppression is required.

The physical GS has no internal scale4 sample grid. Continuously enlarged
triangles introduce samples between the last native centre and the endpoint.
For this sprite, +0.5-native-pixel geometry translation at scale4 is an integer
two-physical-pixel translation. It leaves the within-sprite sample phases
unchanged: the model still reaches two columns at x88 and two rows at y2.
Native-origin correction alone therefore does not solve the scale4 screenshot.

For enlarged point-filtered HUD sprites, a strict native-footprint policy
could rasterize/sample the logical GS grid and enlarge that result. A smoother
policy could constrain enlarged samples to the native sampled footprint.
Both are renderer policies beyond native hardware behavior, requiring guards
for fractional endpoints, reverse UV, nonunit slope, repeat/clamp, intentional
borders, HD textures and existing HUD transforms. Neither should be replaced
by a global half-texel offset or atlas-specific masking.

## Existing test gap

`test_synthetic.cpp::seamTest` uses solid red/blue textures, hiding coordinate
selection changes. Its endpoints8..40.625,42..80 and the menu endpoints
170..340.625,342..512 select the same pixels under either centre convention.
The menu's gap at341 remains real under the integer GS rule.

Its lower vertical sprite starts at133.5: GS first covers row134, while the
current D3D11 path also covers133. The test checks rows132 and134, omitting
the distinguishing row133. Format-decode tests validate decoded data, not the
interpolation origin. Other scene checks mostly probe interior colours.
The earlier README claim that these checks established the GS centre
convention was too strong. Shared renderer/test expectations were not edited
during main's integration work.

The local PS2Recomp software backend also interpolates sprites with an added
0.5, so agreement with it is not independent hardware evidence here.

## CPU checks and proposed GIF regression

`analysis/scripts/test_gs_sprite_sampling.py` adds12 asset-free CPU checks with
exact rational affine arithmetic: native coverage/conversion, half-integer
texel weights, global-offset counterexamples, the captured native footprint,
intentional borders and the scale4 distinction.

```
python -m unittest discover -s analysis/scripts -p test_gs_sprite_sampling.py -v
```

Result:12 tests, zero failures. This does not execute production HLSL, PCSX2,
GS hardware or a renderer fix. The earlier12 HUD packet/atlas checks also pass.

The next actual renderer regression should use native scale1, identity HUD
transform, point filtering, texture replacement/upscaling and seam fix disabled,
and a non-aliased CT32 texture with distinct texel colours. Test both FST and
STQ/Q=1 submission. Required cases:

1. X8..12, UV0.5..4.5: expect texels0,1,2,3; current source math selects1,2,3,4.
   Repeat with linear filtering and exact texel-centre colours.
2. X8.0625..12.0625: pixel8 untouched, pixel12 written, independently testing
   coverage rather than a UV-only correction.
3. Constant UV1, reversed UV, slope2 and fractional adjacent sprites protect
   authored edges, filter weights and shared boundaries.
4. Synthetic perimeter atlas with the captured60x58/Q=1 mapping: no native
   border. A separate intentionally included perimeter must remain visible.
5. Scale4 against an explicitly chosen enlarged-HUD policy. Do not present the
   native oracle as hardware-defined scale4 behavior.

Capture the failing renderer pixels before changing a hunk. Then coordinate
the exact draw/shader scope and GPU/WARP slot with main, compare the candidate
to those fixtures, and perform native/SR HUD image validation. That renderer
failing-before/passing-after gate remains outstanding. No slot was used here.
