# Hook points for the narration overlay (docs/reference/subtitles.md section 6)

1. runtime/hle `ResourceService::handleRequest`: after the request is decoded add
   `if (observer_) observer_(ResourceId{file,path,type,id,sub}, offset);` (API: `void setRequestObserver(std::function<void(const ResourceId&, uint32_t)>)`).
   The log line that proves the key is already printed: `[hle] resource: ENGLISH\LEVELS\LEVEL0RO\LEVEL0RO.SDB 0304/99/14 +0 len ...`.
2. `drakan_overrides.cpp` (host side), trigger table:
```cpp
// intro: id 99, outro: id 100, both sub 14, file LEVEL0RO.SDB of the active language tree
res.setRequestObserver([](const hle::ResourceId& r, uint32_t off) {
    if (off == 0 && r.type == 0x304 && r.sub == 14 && (r.id == 99 || r.id == 100) && endsWithI(r.file, "LEVEL0RO.SDB"))
        g_subs.start(dsubs::loadSrt(drakan_cfg::exeDir() + "overrides\subtitles\\" + lang + (r.id == 99 ? "\intro.srt" : "\outro.srt")));
});
```
3. Stop: `hookMovieDone` (0x440610, already present) and the screen change; pause: `drakan_settings_host` open/close and the input module `menu` flag.
4. Draw: in the gsr overlay callback (see runtime/settings/README.md section 2) call `dsubs::draw(g_subs, oc.width, oc.height, uiScale)` inside the overlay's ImGui frame
   (a second tiny ImGui context if the settings overlay is closed - `render()` returns early when closed, so create the frame in the same callback before that early return).
5. Game's own bar: force it on with `*(u32*)(rdram + 0x4fb740) = 1` once at boot only if `[subtitles] game = 1` and the memory card has no saved settings
   (the game loads the table at 0x54ba18 from the card, an old save with Subtitles off wins afterwards - so write it after the settings load, e.g. when the title menu appears).
