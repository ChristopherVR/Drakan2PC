// Drakan: the pictures the host screens show, as overlay textures: save slot thumbnails / HD companions, map and
// game cover pictures, the texture pack's title art, setting screenshots and live character models.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
// A slot's picture as a texture: the HD companion when the save has one, else the native 128 x 128 thumbnail (a 4:3
// picture: the native grid draws it at 128 x 96). The companions (about 3 MB of PNG each) are decoded on a worker
// thread (std::async) so neither the present thread nor the screen waits for them; the native thumbnail (or the
// crest) stands in until the decode is done. A decode whose slot changed meanwhile is dropped when it finishes.

PreviewDecode decodePreview(std::string path) {
    PreviewDecode r;
    const auto t0 = std::chrono::steady_clock::now();
    if (gsr::readImage(path.c_str(), r.img) && r.img) {
        const gsr::Image& img = r.img;
        unsigned x0 = img.width, y0 = img.height, x1 = 0, y1 = 0;
        for (unsigned y = 0; y < img.height; y += 2)
            for (unsigned x = 0; x < img.width; x += 2) {
                const uint8_t* q = img.rgba.data() + (size_t(y) * img.width + x) * 4;
                if (q[0] + q[1] + q[2] > 24) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x + 2);
                    y1 = std::max(y1, y + 2);
                }
            }
        if (x1 <= x0 || y1 <= y0)
            x0 = y0 = 0, x1 = img.width, y1 = img.height;
        r.x0 = x0;
        r.y0 = y0;
        r.x1 = std::min(x1, img.width);
        r.y1 = std::min(y1, img.height);
    } else {
        r.img = {};
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}
} // namespace

std::vector<std::pair<int, PreviewTexture>> g_previews;
std::vector<std::future<PreviewDecode>> g_previewDrops; // decodes of replaced entries, dropped once finished

namespace {
uint64_t g_previewSerial = ~0ull;
} // namespace

ImTextureID makeTexture(const void* rgba, unsigned w, unsigned h) {
    return g_overlay && rgba && w && h ? g_overlay->renderer().createTexture(rgba, w, h) : 0;
}
void dropTexture(ImTextureID& tex) {
    if (tex && g_overlay)
        g_overlay->renderer().releaseTexture(tex);
    tex = 0;
}

namespace {
bool futureReady(const std::future<PreviewDecode>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
} // namespace

// Present thread, before this frame's draw lists (an SRV replaced here is not referenced by a pending draw).

void updatePreviewTextures() {
    updateMapPreviews();
    std::erase_if(g_previewDrops, [](const std::future<PreviewDecode>& f) { return !f.valid() || futureReady(f); });
    auto& sh = drakan_menu_screens::shared();
    std::vector<std::pair<int, drakan_menu_screens::SlotPicture>> pics;
    {
        std::lock_guard<std::mutex> l(sh.m);
        if (sh.picturesSerial != g_previewSerial) {
            g_previewSerial = sh.picturesSerial;
            pics = sh.pictures;
        }
    }
    for (auto& [n, pic] : pics) {
        PreviewTexture* t = nullptr;
        for (auto& p : g_previews)
            if (p.first == n)
                t = &p.second;
        if (!t) {
            g_previews.emplace_back(n, PreviewTexture{});
            t = &g_previews.back().second;
        }
        if (t->key == pic.key && t->hd == pic.hdPath &&
            (t->tex || t->decode.valid() || (!pic.key && pic.hdPath.empty())))
            continue;
        dropTexture(t->tex);
        if (t->decode.valid())
            g_previewDrops.push_back(std::move(t->decode));
        *t = {};
        t->key = pic.key;
        t->hd = pic.hdPath;
        if (!pic.hdPath.empty())
            t->decode = std::async(std::launch::async, decodePreview, pic.hdPath);
        if (pic.key && pic.pixels.size() == 128 * 128) { // the native thumbnail meanwhile (or for good)
            t->tex = makeTexture(pic.pixels.data(), 128, 128);
            // Rows top down, as stored (compared with the native grid's picture of the same save).
            t->uv0 = ImVec2(0, 0);
            t->uv1 = ImVec2(1, 1);
            t->w = 128;
            t->h = 96;
        }
        if (pic.hdPath.empty())
            std::fprintf(stderr, "[menu-screens] overlay: slot %d picture %s\n", n,
                         t->tex ? "native thumbnail" : "none");
    }
    for (auto& [n, t] : g_previews) {
        if (!futureReady(t.decode))
            continue;
        PreviewDecode r = t.decode.get();
        ImTextureID tex = r.img ? makeTexture(r.img.rgba.data(), r.img.width, r.img.height) : 0;
        std::fprintf(stderr, "[menu-screens] overlay: slot %d picture %s (decoded in %.0f ms on a worker, t=%.3f)\n", n,
                     tex ? "HD companion" : (t.tex ? "native thumbnail (HD failed)" : "none"), r.ms,
                     drakan_menu_screens::clockSeconds());
        if (!tex)
            continue;
        dropTexture(t.tex);
        t.tex = tex;
        t.hdShown = true;
        t.uv0 = ImVec2(float(r.x0) / r.img.width, float(r.y0) / r.img.height);
        t.uv1 = ImVec2(float(r.x1) / r.img.width, float(r.y1) / r.img.height);
        t.w = float(r.x1 - r.x0) * drakan_save_previews::kLegacyApertureAspect;
        t.h = float(r.y1 - r.y0);
    }
}

// ---- multiplayer map pictures ----
// A map's picture is optional: `previews/<map name>.png` next to the exe (the player's own capture or a mod pack's
// image; nothing from the disc is stored in the repository). The menu draws a generated sketch when there is none.
// Which folders count and when a match captures one: drakan_map_preview_policy.h.
std::mutex g_mapPreviewM;
std::map<uint32_t, std::string> g_mapPreviewPaths; // previewId -> file
std::map<uint32_t, PreviewTexture> g_mapPreviews;

// DRAKAN_MP_CAPTURE=1: a match on a map without a picture captures the current view (off by default, issue #1).
bool mapAutoCapture() {
    static const bool on = drakan_map_preview::autoCaptureEnabled(std::getenv("DRAKAN_MP_CAPTURE"));
    return on;
}

namespace {
// An explicit picture folder (DRAKAN_PREVIEW_DIR), or "" when there is none.
std::string explicitPreviewDir() {
    if (const char* e = std::getenv("DRAKAN_PREVIEW_DIR"); e && *e)
        return std::string(e) + "/";
    return {};
}
} // namespace

// Where the game's own map pictures are cached: the user's data folder (DRAKAN_PREVIEW_DIR overrides), never the
// repository or the play folder. Falls back to previews/captured/ next to the exe when there is none.
std::string capturedPreviewDir() {
    if (std::string e = explicitPreviewDir(); !e.empty())
        return e;
#ifdef _WIN32
    if (const char* l = std::getenv("LOCALAPPDATA"); l && *l)
        return std::string(l) + "/Drakan2PC/previews/";
#else
    if (const char* h = std::getenv("HOME"); h && *h)
        return std::string(h) + "/.local/share/Drakan2PC/previews/";
#endif
    return drakan_cfg::exeDir() + "previews/captured/";
}

std::string mapSafeName(const std::string& mapName) {
    std::string safe;
    for (char c : mapName)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return safe;
}

// previews/<name>.png (the player's own picture) wins, then previews/mods/<name>.png (drakan_overrides copies the
// previews/<name>.png of every loaded mod pack there), then DRAKAN_PREVIEW_DIR, then (DRAKAN_MP_CAPTURE=1 only) the
// game's own captures of the map in a match (mpPreviewCapture) in the user's data folder.
uint32_t mapPreviewId(const std::string& mapName) {
    struct Known {
        uint32_t id = 0;
        std::chrono::steady_clock::time_point checked;
    };
    static std::map<std::string, Known> cache; // name -> id (0 = no file, looked at again every 3 s)
    std::lock_guard<std::mutex> l(g_mapPreviewM);
    const auto now = std::chrono::steady_clock::now();
    auto it = cache.find(mapName);
    if (it != cache.end() && (it->second.id != 0 || now - it->second.checked < std::chrono::seconds(3)))
        return it->second.id;
    const std::string safe = mapSafeName(mapName);
    std::error_code ec;
    uint32_t id = 0;
    for (const std::string& base : drakan_map_preview::searchDirs(drakan_cfg::exeDir(), explicitPreviewDir(),
                                                                  capturedPreviewDir(), mapAutoCapture())) {
        const std::string path = base + safe + ".png";
        if (!std::filesystem::exists(path, ec))
            continue;
        id = 2166136261u;
        for (unsigned char c : path)
            id = (id ^ c) * 16777619u;
        id |= 1u;
        g_mapPreviewPaths[id] = path;
        break;
    }
    cache[mapName] = {id, now};
    return id;
}

// The disc game's title picture for the Original game list: the texture pack's wide title set, the image the renderer
// draws behind the title menu in a wide window (<texture_dir>/wide/screen8_<a>x<b>.png, widescreen.md "Wide menu
// backdrops"), picked for the view's aspect by the renderer's own rule. 0 = the pack has none (looked at again every
// 3 s). The folder is the renderer's: GSR_TEXDIR, else [video] texture_dir next to the exe.
uint32_t titleArtPictureId(float viewAspect) {
    struct Wide {
        float aspect = 0;
        std::string path;
    };
    static std::vector<Wide> set;
    static std::chrono::steady_clock::time_point checked;
    std::lock_guard<std::mutex> l(g_mapPreviewM);
    const auto now = std::chrono::steady_clock::now();
    if (set.empty() && (checked.time_since_epoch().count() == 0 || now - checked > std::chrono::seconds(3))) {
        checked = now;
        std::string dir;
        if (const char* e = std::getenv("GSR_TEXDIR"); e && *e) {
            dir = e;
        } else {
            dir = drakan_cfg::getString("video", "texture_dir", nullptr, "textures");
            if (dir.empty())
                dir = "textures";
            if (dir.size() < 2 || (dir[1] != ':' && dir[0] != '\\' && dir[0] != '/'))
                dir = drakan_cfg::exeDir() + dir;
        }
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir + "/wide", ec)) {
            const float aspect = drakan_title_picture::wideAspectOf(e.path().filename().string());
            if (aspect > 0)
                set.push_back({aspect, e.path().generic_string()});
        }
        std::sort(set.begin(), set.end(), [](const Wide& a, const Wide& b) { return a.path < b.path; });
    }
    std::vector<float> aspects;
    for (const Wide& w : set)
        aspects.push_back(w.aspect);
    const int pick = drakan_title_picture::pickWide(aspects, viewAspect);
    if (pick < 0)
        return 0;
    const std::string& path = set[static_cast<size_t>(pick)].path;
    uint32_t id = 2166136261u;
    for (unsigned char c : path)
        id = (id ^ c) * 16777619u;
    id |= 1u;
    g_mapPreviewPaths[id] = path;
    return id;
}

// How far a picture of mapPreviewId / titleArtPictureId has got (the render thread's view of g_mapPreviews).
drakan_title_picture::Load pictureLoad(uint32_t id) {
    using drakan_title_picture::Load;
    if (id == 0)
        return Load::Missing;
    const auto it = g_mapPreviews.find(id);
    if (it == g_mapPreviews.end())
        return Load::Pending; // the next updateMapPreviews starts its decode
    if (it->second.tex)
        return Load::Ready;
    return it->second.decode.valid() ? Load::Pending : Load::Failed;
}

namespace {
// ---- real pictures of the settings (gameui/SettingPreview.h) ----
// previews/settings/<row key>/<value>.png next to the exe: screenshots of this game with that value applied, written by
// scripts/harness/setting_shots.py on the player's machine (never committed). A value without its own file takes the
// nearest numeric one (a slider between two captures); nothing at all means the help panel draws no real picture.
std::set<uint32_t> g_settingShotIds; // picture ids that are stretched to the 16:9 box, not centre cropped
} // namespace

uint32_t settingShotPicture(const std::string& key, const std::string& value) {
    struct Capture {
        std::string value, path;
        double number = 0;
        bool numeric = false;
    };
    struct Folder {
        std::vector<Capture> files;
        std::chrono::steady_clock::time_point checked;
    };
    static std::map<std::string, Folder> cache;
    std::lock_guard<std::mutex> l(g_mapPreviewM);
    const auto now = std::chrono::steady_clock::now();
    Folder& f = cache[key];
    if (f.checked.time_since_epoch().count() == 0 || now - f.checked > std::chrono::seconds(5)) {
        f.checked = now;
        f.files.clear();
        std::error_code ec;
        for (const auto& e :
             std::filesystem::directory_iterator(drakan_cfg::exeDir() + "previews/settings/" + key, ec)) {
            if (e.path().extension() != ".png")
                continue;
            Capture c;
            c.value = e.path().stem().string();
            c.path = e.path().generic_string();
            char* end = nullptr;
            c.number = std::strtod(c.value.c_str(), &end);
            c.numeric = end && end != c.value.c_str() && *end == 0;
            f.files.push_back(std::move(c));
        }
    }
    std::string safe = value;
    std::replace(safe.begin(), safe.end(), ':', 'x');
    const Capture* best = nullptr;
    for (const Capture& c : f.files)
        if (c.value == safe)
            best = &c;
    if (!best) {
        char* end = nullptr;
        const double v = std::strtod(value.c_str(), &end);
        if (end && end != value.c_str() && *end == 0)
            for (const Capture& c : f.files)
                if (c.numeric && (!best || std::abs(c.number - v) < std::abs(best->number - v)))
                    best = &c;
    }
    if (!best)
        return 0;
    uint32_t id = 2166136261u;
    for (unsigned char c : best->path)
        id = (id ^ c) * 16777619u;
    id |= 1u;
    g_mapPreviewPaths[id] = best->path;
    g_settingShotIds.insert(id);
    return id;
}

void updateMapPreviews() {
    std::vector<std::pair<uint32_t, std::string>> want;
    {
        std::lock_guard<std::mutex> l(g_mapPreviewM);
        for (const auto& [id, path] : g_mapPreviewPaths)
            if (!g_mapPreviews.count(id))
                want.emplace_back(id, path);
    }
    for (auto& [id, path] : want) {
        PreviewTexture& t = g_mapPreviews[id];
        t.hd = path;
        t.decode = std::async(std::launch::async, decodePreview, path);
    }
    for (auto& [id, t] : g_mapPreviews) {
        if (!futureReady(t.decode))
            continue;
        PreviewDecode r = t.decode.get();
        ImTextureID tex = r.img ? makeTexture(r.img.rgba.data(), r.img.width, r.img.height) : 0;
        std::fprintf(stderr, "[menu-screens] map picture %08x: %s (%ux%u)\n", id, tex ? "loaded" : "failed",
                     r.img.width, r.img.height);
        if (!tex)
            continue;
        t.tex = tex;
        // The game's own captures (the player's data folder) include the native HUD: orbs along the bottom, the stats
        // overlay at the top left. The menu shows the band between them.
        const bool own =
            t.hd.rfind(capturedPreviewDir(), 0) == 0 || t.hd.find("previews/captured/") != std::string::npos;
        t.uv0 = ImVec2(0, own ? 0.08f : 0.0f);
        t.uv1 = ImVec2(1, own ? 0.74f : 1.0f);
        t.w = float(r.img.width);
        t.h = float(r.img.height) * (own ? 0.66f : 1.0f);
    }
}

std::map<int, MenuCharacterModel> g_characterModels;
bool characterPreviewReady(int classId) {
    MenuCharacterModel& t = g_characterModels[classId];
    if (t.model && t.model->ready())
        return true;
    const auto now = std::chrono::steady_clock::now();
    if (!t.started && (!t.failed || now - t.checked > std::chrono::seconds(5))) {
        char name[24];
        std::snprintf(name, sizeof name, "%04x.dcpm", static_cast<unsigned>(classId) & 0xffffu);
        const std::string path = drakan_cfg::exeDir() + "previews/chars/" + name;
        t.checked = now;
        t.started = true;
        t.decode = std::async(std::launch::async, [path] { return dsettings::CharacterPreview::load(path); });
    }
    if (t.started && t.decode.valid() && t.decode.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        t.model = t.decode.get();
        t.started = false;
        t.failed = !t.model || !t.model->upload(makeTexture, [](ImTextureID id) { dropTexture(id); });
        if (!t.failed)
            std::fprintf(stderr, "[menu-screens] live character model %04x: loaded (%zu triangles)\n", classId,
                         t.model->triangleCount());
    }
    return !t.failed && t.model && t.model->ready();
}

bool previewCrop(const gameui::ImageRef& ref, dsettings::IconCrop& c) {
    if (ref.kind == gameui::ImageKind::CharacterModel) {
        const auto it = g_characterModels.find(static_cast<int>(ref.index));
        if (it == g_characterModels.end() || !it->second.model || !it->second.model->ready())
            return false;
        c.model = it->second.model.get();
        return true;
    }
    if (ref.kind == gameui::ImageKind::LevelPreview) {
        const auto it = g_mapPreviews.find(ref.index);
        if (it == g_mapPreviews.end() || !it->second.tex)
            return false;
        c.texture = it->second.tex;
        c.uv0 = it->second.uv0;
        c.uv1 = it->second.uv1;
        c.w = it->second.w;
        c.h = it->second.h;
        {
            std::lock_guard<std::mutex> l(g_mapPreviewM);
            c.stretch = g_settingShotIds.count(ref.index) != 0; // a settings capture shows the whole frame
        }
        return true;
    }
    if (ref.kind != gameui::ImageKind::SavePreview)
        return false; // TitleArt: the native art shows under the menu (TitleState::backdrop stays None)
    for (const auto& [n, t] : g_previews)
        if (n == static_cast<int>(ref.index) && t.tex) {
            c.texture = t.tex;
            c.uv0 = t.uv0;
            c.uv1 = t.uv1;
            c.w = t.w;
            c.h = t.h;
            return true;
        }
    return false;
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
