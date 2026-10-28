// Keyboard/mouse replacements for the DualShock prompts of Drakan: The Ancients' Gates (SCES-500.06). See
// docs/reference/ui-prompts.md.
//
//   INTERFCE.TXD  0x40/97   plain UI font: 8 button glyphs (text escapes \030..\037: Circle Square Triangle Cross,
//   arrows left right up down) INTERFCE.TXD  0x40/124  controller diagram of Options > Controller (leader lines are
//   kept, the pad body is replaced by key caps) INTERFCE.TXD  0x40/94   inventory icon atlas: the "R1 / L1" page-switch
//   tile
//   *.XDB         0x400     text strings: "L1", "R2", "START" ... and menu glyph escapes rewritten to the bound key
//   names
//
// Everything is generated from the original resources at load time (the override sees the disc data) according to
// drakan_input.ini.
#pragma once
#include "hle/ResourceOverride.h"
#include "hle/ResourceService.h"
#include "ui/Bindings.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drakan_ui {

class PromptOverride final : public hle::IResourceOverride {
public:
    explicit PromptOverride(Bindings b) : b_(std::move(b)) {}
    bool wants(const hle::ResourceId& id) override;
    bool apply(const hle::ResourceId& id, std::vector<hle::u8>& data) override;
    // The individual generators (exposed for tests / the preview tool). Return an empty vector when the input does not
    // look right.
    static std::vector<hle::u8> patchFont(const std::vector<hle::u8>& blob, const Bindings& b);              // 0x40/97
    static std::vector<hle::u8> patchControllerDiagram(const std::vector<hle::u8>& blob, const Bindings& b); // 0x40/124
    static std::vector<hle::u8> patchPageTile(const std::vector<hle::u8>& blob, const Bindings& b);          // 0x40/94
    // Called for every texture the generator rewrote, with the decoded result in stored row order (R in the low byte,
    // alpha in PS2 units 0..0x80 = what a GS renderer decodes from the uploaded texture). The runtime uses it to keep
    // these binding-dependent textures out of the HD texture dump and to give them HD versions.
    struct Generated {
        unsigned id = 0;                    // INTERFCE.TXD texture id (94, 96, 97, 124)
        const uint32_t* rgba = nullptr;     // the generated texture
        const uint32_t* original = nullptr; // the disc texture before the rewrite (same size and convention)
        int w = 0, h = 0;
        std::string pageNext, pagePrev; // id 94: captions of the page-switch buttons (R1 / L1, menu layer)
    };
    using GeneratedHook = std::function<void(const Generated&)>;
    // Short caption of a key for the page-switch buttons ("PgUp", "E", "LMB").
    static std::string pageCaption(const Label& l);
    void setGeneratedHook(GeneratedHook h) { hook_ = std::move(h); }
    // Skip entries that a DirectoryOverride provides itself.
    void setUserOverride(std::shared_ptr<hle::DirectoryOverride> d) { user_ = std::move(d); }

private:
    Bindings b_;
    std::shared_ptr<hle::DirectoryOverride> user_;
    GeneratedHook hook_;
};

// Text rewrite rule for hle::XdbTextOverride (see Prompts.cpp for the rules).
hle::XdbTextOverride::Rule makeTextRule(const Bindings& b);
// The rewrite itself, exposed for tests. `file` = upper-case file name, `id` = text id, `lang` 0..4.
bool rewriteText(const Bindings& b, const std::string& file, unsigned id, int lang, std::string& text);

struct Options {
    std::string iniPath;                       // drakan_input.ini
    std::string overridesDir;                  // overrides/<FILE>/...
    bool prompts = true;                       // generate the keyboard/mouse prompts
    PromptOverride::GeneratedHook onGenerated; // see PromptOverride::setGeneratedHook
    // Environment (DRAKAN_INPUT_INI, DRAKAN_OVERRIDES, DRAKAN_UI_PROMPTS=0) + executable directory / working directory
    // lookup.
    static Options fromEnvironment();
};
// Registers the override layer on the resource service: user override folder first, then the generated prompts. Returns
// a one-line summary.
std::string install(hle::ResourceService& res, const Options& opt);

} // namespace drakan_ui
