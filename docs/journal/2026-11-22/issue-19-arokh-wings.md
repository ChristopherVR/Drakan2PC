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

The failure remains unconfirmed on current source. The exact older executable, video configuration and installed
replacement images from the reporter's setup were unavailable. Their `drakan.ini` and a repeat with HD texture
replacement off would test whether the failure depends on texture replacement.
