// Drakan: read-only world introspection adapter and F1 "World" panel (drakan_world.h).
#include "drakan_world.h"
#include "drakan_config.h"
#include "drakan_ghost.h"
#include "drakan_region.h"
#include "imgui.h"
#include "ps2_runtime.h"
#include "world/Exchange.h"
#include "world/Json.h"
#include "world/Reader.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace drakan_world {
namespace {

class RdramMemory final : public world::IGuestMemory {
public:
    explicit RdramMemory(const uint8_t* ram) : ram_(ram) {}
    uint8_t read8(uint32_t a) const override {
        a &= 0x1ffffffu;
        return a < PS2_RAM_SIZE ? ram_[a] : 0;
    }
    uint32_t read32(uint32_t a) const override {
        a &= 0x1ffffffu;
        if (a + 4 > PS2_RAM_SIZE)
            return 0;
        uint32_t v;
        std::memcpy(&v, ram_ + a, 4);
        return v;
    }

private:
    const uint8_t* ram_;
};

std::atomic<bool> g_enabled{false};
world::Globals g_globals; // EE thread (set once in configure, before the first sample)
world::ReaderState g_state;
world::SnapshotExchange g_exchange;
bool g_headless = false;    // DRAKAN_WORLD=2: sample every frame without the overlay (harness runs)
uint64_t g_dumpAtFrame = 0; // DRAKAN_WORLD_DUMP_FRAME=N: write one JSON dump at the Nth sample (harness runs)
uint64_t g_frames = 0;

// ---- UI thread state ----
struct Ui {
    char classFilter[16] = "";
    int typeFilter = -1; // -1 = all
    bool hpOnly = false;
    bool onlyListed = false;
    bool sortByDistance = true;
    float maxDistance = 0; // 0 = no limit
    uint32_t selected = 0;
    std::string status;
    std::vector<uint32_t> typesSeen;
    std::vector<const world::EntitySnapshot*> rows;
};
Ui g_ui;

const char* typeLabel(uint32_t t) {
    switch (t) {
        case world::type::kPlayer:
            return "player";
        case world::type::kCreatureA:
        case world::type::kCreatureB:
            return "creature";
        case world::type::kWeapon:
            return "weapon";
        case world::type::kUsable:
            return "usable";
        default:
            return "";
    }
}

std::string dumpDir() {
    return drakan_cfg::exeDir() + "dev" + std::string(1, std::filesystem::path::preferred_separator) + "world";
}

void dumpJson(const world::Snapshot& s) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dumpDir(), ec);
    std::string level = s.levelName;
    for (char& c : level)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    char name[96];
    std::snprintf(name, sizeof name, "world_%s_%06llu.json", level.empty() ? "unknown" : level.c_str(),
                  static_cast<unsigned long long>(s.sequence));
    const fs::path path = fs::path(dumpDir()) / name;
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        g_ui.status = "cannot write " + path.string();
        return;
    }
    f << world::toJson(s);
    g_ui.status = "wrote " + path.string();
    std::fprintf(stderr, "[world] %s (%zu entities)\n", g_ui.status.c_str(), s.entities.size());
}

bool matchesClass(uint32_t classId, const char* filter) {
    if (!*filter)
        return true;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", classId);
    std::string f = filter;
    if (f.rfind("0x", 0) == 0 || f.rfind("0X", 0) == 0)
        f = f.substr(2);
    for (char& c : f)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return std::strstr(buf, f.c_str()) != nullptr;
}

} // namespace

void configure() {
    const char* v = std::getenv("DRAKAN_WORLD");
    if (!v || !*v || v[0] == '0')
        return;
    world::Globals g;
    g.contextPtr = drakan_region::guestAddress(g.contextPtr);
    g.hpKey = drakan_region::guestAddress(g.hpKey);
    g.playerStats = drakan_region::guestAddress(g.playerStats);
    g.handleVtable = drakan_region::guestAddress(g.handleVtable);
    if (!g.contextPtr || !g.hpKey || !g.playerStats || !g.handleVtable) {
        std::fprintf(stderr, "[world] disabled: this disc profile has no address translation for the world model\n");
        return;
    }
    g_globals = g;
    g_headless = v[0] == '2';
    if (const char* d = std::getenv("DRAKAN_WORLD_DUMP_FRAME"))
        g_dumpAtFrame = std::strtoull(d, nullptr, 10);
    g_enabled.store(true);
    std::fprintf(stderr, "[world] panel enabled (F1 > World window); dumps go to %s\n", dumpDir().c_str());
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void sample(const uint8_t* rdram, bool overlayOpen) {
    if (!g_enabled.load(std::memory_order_relaxed) || !rdram || !(overlayOpen || g_headless))
        return;
    const RdramMemory mem(rdram);
    world::readSnapshot(mem, g_globals, g_state, g_exchange.back());
    {
        // Log status transitions (level load, save / load rebuild the registry) so harness runs can check recovery.
        static world::ReadStatus last = world::ReadStatus::Ok;
        static bool first = true;
        static size_t lastCount = 0;
        static std::string lastLevel;
        const world::Snapshot& cur = g_exchange.back();
        if (first || cur.status != last || cur.levelName != lastLevel ||
            (cur.status == world::ReadStatus::Ok && lastCount == 0)) {
            std::fprintf(stderr, "[world] sample status %s, %zu entities, level '%s'\n",
                         world::readStatusName(cur.status), cur.entities.size(), cur.levelName.c_str());
            first = false;
        }
        last = cur.status;
        lastLevel = cur.levelName;
        lastCount = cur.entities.size();
    }
    if (g_dumpAtFrame && g_exchange.back().status == world::ReadStatus::Ok && g_exchange.back().player() &&
        ++g_frames == g_dumpAtFrame)
        dumpJson(g_exchange.back()); // Nth sample with a live player (harness runs)
    g_exchange.publish();
}

void drawPanel() {
    if (!g_enabled.load(std::memory_order_relaxed))
        return;
    const std::shared_ptr<const world::Snapshot> snap = g_exchange.acquire();
    // Size from the display and font, not fixed pixels: the overlay font scales with the output (4K = about 24 px).
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(12, em * 4.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(display.x * 0.6f, em * 60.0f), std::min(display.y * 0.8f, em * 36.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (!snap) {
        ImGui::TextUnformatted("No snapshot yet (the guest has not run a frame with the overlay open).");
        ImGui::End();
        return;
    }
    const world::Snapshot& s = *snap;
    if (s.status != world::ReadStatus::Ok) {
        ImGui::Text("Guest world not available: %s", world::readStatusName(s.status));
        ImGui::End();
        return;
    }
    const world::EntitySnapshot* player = s.player();
    ImGui::Text("Level: %s   entities: %zu   slots: %u / %u   stale: %u", s.levelName.c_str(), s.entities.size(),
                s.registryHighWater + 1, s.registryCapacity, s.staleSlots);
    ImGui::TextDisabled("%s", s.levelPath.c_str());
    if (player)
        ImGui::Text("Player %08x  class %05x  pos %.0f %.0f %.0f  HP %d/%d", player->handle, player->classId,
                    static_cast<double>(player->position.x), static_cast<double>(player->position.y),
                    static_cast<double>(player->position.z), player->hp, player->hpMax);

    // ---- filters ----
    g_ui.typesSeen.clear();
    for (const auto& e : s.entities)
        if (std::find(g_ui.typesSeen.begin(), g_ui.typesSeen.end(), e.type) == g_ui.typesSeen.end())
            g_ui.typesSeen.push_back(e.type);
    std::sort(g_ui.typesSeen.begin(), g_ui.typesSeen.end());
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("class (hex)", g_ui.classFilter, sizeof g_ui.classFilter);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    char typeText[48];
    if (g_ui.typeFilter < 0)
        std::snprintf(typeText, sizeof typeText, "all types");
    else
        std::snprintf(typeText, sizeof typeText, "type %d %s", g_ui.typeFilter,
                      typeLabel(static_cast<uint32_t>(g_ui.typeFilter)));
    if (ImGui::BeginCombo("##type", typeText)) {
        if (ImGui::Selectable("all types", g_ui.typeFilter < 0))
            g_ui.typeFilter = -1;
        for (const uint32_t t : g_ui.typesSeen) {
            char label[48];
            std::snprintf(label, sizeof label, "type %u %s", t, typeLabel(t));
            if (ImGui::Selectable(label, g_ui.typeFilter == static_cast<int>(t)))
                g_ui.typeFilter = static_cast<int>(t);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("has HP", &g_ui.hpOnly);
    ImGui::SameLine();
    ImGui::Checkbox("in lists", &g_ui.onlyListed);
    ImGui::SameLine();
    ImGui::Checkbox("nearest first", &g_ui.sortByDistance);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("max distance (0 = any)", &g_ui.maxDistance, 0.0f, 100000.0f, "%.0f");
    ImGui::SameLine();
    if (ImGui::Button("Dump JSON"))
        dumpJson(s);
    if (!g_ui.status.empty())
        ImGui::TextDisabled("%s", g_ui.status.c_str());
    drakan_ghost::drawControls();

    // ---- rows ----
    const world::Vec3 origin = player ? player->position : world::Vec3{};
    g_ui.rows.clear();
    for (const auto& e : s.entities) {
        if (g_ui.typeFilter >= 0 && e.type != static_cast<uint32_t>(g_ui.typeFilter))
            continue;
        if (!matchesClass(e.classId, g_ui.classFilter))
            continue;
        if (g_ui.hpOnly && !e.hasHp())
            continue;
        if (g_ui.onlyListed && e.listMask == 0)
            continue;
        if (g_ui.maxDistance > 0 && world::distance(e.position, origin) > g_ui.maxDistance)
            continue;
        g_ui.rows.push_back(&e);
    }
    if (g_ui.sortByDistance)
        std::sort(g_ui.rows.begin(), g_ui.rows.end(),
                  [&](const world::EntitySnapshot* a, const world::EntitySnapshot* b) {
                      return world::distance(a->position, origin) < world::distance(b->position, origin);
                  });
    ImGui::Text("%zu shown", g_ui.rows.size());

    const float detailsH = ImGui::GetTextLineHeightWithSpacing() * 5.5f;
    if (ImGui::BeginTable("##ents", 8,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0, -detailsH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("handle");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("class");
        ImGui::TableSetupColumn("res");
        ImGui::TableSetupColumn("model");
        ImGui::TableSetupColumn("position");
        ImGui::TableSetupColumn("dist");
        ImGui::TableSetupColumn("hp");
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(g_ui.rows.size()));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const world::EntitySnapshot& e = *g_ui.rows[static_cast<size_t>(i)];
                const bool isPlayer = e.handle == s.playerHandle;
                ImGui::TableNextRow();
                if (isPlayer)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 120, 40, 140));
                ImGui::TableSetColumnIndex(0);
                char id[24];
                std::snprintf(id, sizeof id, "%08x##%d", e.handle, i);
                if (ImGui::Selectable(id, g_ui.selected == e.handle, ImGuiSelectableFlags_SpanAllColumns))
                    g_ui.selected = e.handle;
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u %s", e.type, isPlayer ? "PLAYER" : typeLabel(e.type));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%05x", e.classId);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", e.resourceIndex);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%x", e.modelId);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.0f %.0f %.0f", static_cast<double>(e.position.x), static_cast<double>(e.position.y),
                            static_cast<double>(e.position.z));
                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%.0f", static_cast<double>(world::distance(e.position, origin)));
                ImGui::TableSetColumnIndex(7);
                if (e.hasHp())
                    ImGui::Text("%d/%d", e.hp, e.hpMax);
            }
        }
        ImGui::EndTable();
    }
    if (const world::EntitySnapshot* e = s.find(g_ui.selected)) {
        ImGui::Text("handle %08x  ref %u  refcount %u  type %u  class %05x  res index %u  res id %05x", e->handle,
                    e->ref, e->refCount, e->type, e->classId, e->resourceIndex, e->resourceId);
        ImGui::Text("model %x  flags %08x  lists %02x  yaw %.1f deg  distance %.0f", e->modelId, e->flags, e->listMask,
                    static_cast<double>(e->yaw * 57.29578f), static_cast<double>(world::distance(e->position, origin)));
        ImGui::Text("pos %.1f %.1f %.1f", static_cast<double>(e->position.x), static_cast<double>(e->position.y),
                    static_cast<double>(e->position.z));
        if (e->hasHp())
            ImGui::Text("%s %d / %d", e->hpKind == world::HpKind::Durability ? "durability" : "HP", e->hp, e->hpMax);
    } else {
        ImGui::TextDisabled("Select a row for details (read only).");
    }
    ImGui::End();
}

} // namespace drakan_world
