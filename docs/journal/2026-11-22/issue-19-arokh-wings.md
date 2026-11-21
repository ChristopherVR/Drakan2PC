# Issue 19: Arokh's wings in the opening scene

The report shows Arokh's shoulder claws and body after he lands in The Ancients' Gates, with the wing membranes missing.
The report names build `5cedb764`. The Direct3D 11 log shows PAL, an RTX 5070 Ti, a 21:9 view at internal scale 3,
and DLSS Quality. The rotated log shows WebGPU over Vulkan. Both list Order of the Flame pack `5028d2c84dfcdae2` and character
motion pack `28306eff511800af`. [Issue and attachments](https://github.com/ChristopherVR/Drakan2PC/issues/19)

On the local RTX 5080, Arokh's wings rendered during his approach, landing and close-up on Direct3D 11 and WebGPU
(Vulkan). The Direct3D 11 checks included a run with original textures, an HD replacement run, and a run at 21:9 and
internal scale 3 with DLSS Quality and both exact reported mod builds enabled. In the last run, the entire narration
movie played before the in-engine scene. Captures `scene32.png` and `scene38.png` in
`build/issues-18-19/full-report/` show the landing and the close-up from the report. The Vulkan run had HD replacements,
21:9 and internal scale 3, using the current Order of the Flame pack.

## Reproduction with an HD replacement

The PAL model's wing membranes use entry 5430 in `SYSTEM.TXD`, decoded content hash `0280f875945ec026` in all five
disc languages. The wing draw samples the 32-bit texture, blends with its alpha and rejects pixels below the alpha
threshold. The claws and body sample other textures.

An exact-match HD replacement could override that alpha. With the RGB of the generated HD image unchanged and its
alpha set to zero, the intro reproduced the report's close-up with the body and claws visible and the membranes
missing. The runtime now uses the native alpha with the HD RGB even for exact replacements of this texture. This
reuses the alpha handling for RGB aliases. The reporter's original replacement images were unavailable.

With the same transparent file and both reported mods, the rebuilt game showed the wings on Direct3D 11 and
WebGPU over Vulkan and Direct3D 12. The logs confirmed that each renderer used the 4x replacement. The two
pre-fix runs lost the wings on Direct3D 11 and Vulkan. See `build/issue19/compare.jpg` for the matched close-ups.
The texture scaling, texture decoding, software HD texture and two synthetic renderer suites passed.
