# Sketch: stop CLUT-animated textures from flooding the dump (runtime/gs, for the gs owner)

In `Impl::setupUpscale` (d3d11_backend.cpp), after `const uint64_t hash = texup::hashContent(...)`:

```cpp
// indexed textures only (key.clutKind != 0). The raw index plane is available while decodeLevel walks the block memory.
uint64_t idx = indexed ? texup::hashContent(rawIndicesPackedAsU32.data(), W, H) ^ 0xA5A5A5A5ull : 0;
Family& fam = families[idx];            // std::unordered_map<uint64_t, Family>{ std::unordered_set<uint64_t> contents; bool isVolatile; }
if (indexed && fam.contents.insert(hash).second && fam.contents.size() > 4) fam.isVolatile = true;
if (fam.isVolatile) { appendLine(texDir + "/volatile.txt", hashName(idx)); wantDump = false; replace = false; }
```

* dump at most 2 variants per family;
* replacement lookup only for non volatile families; volatile ones keep the live xBRZ/smooth upscale (cache is keyed by content hash, one job per CLUT state);
* `writePngRGBA`: replace the stored-deflate encoder by real deflate (miniz / stb_image_write), 4-8x smaller files;
* optional marker `<hash>.a0` for dumps whose alpha is constant 0 (CT24 tiles with TEXA 0) so external tools never premultiply them.
