// Every guest address and offset the modern Tab screen reads, in one place.
// Source: docs/journal/2026-10-24/tab-rework-data-model.md (PAL SCES-500.06). [RAM] = checked in a dump, [dec] = read
// in the decompile, [inf] = inferred. Nothing here is written to; the reader only reads.
#pragma once
#include <cstdint>

namespace gameui::addr {

// ---- globals ----
inline constexpr uint32_t kContextPtr = 0x5565d4;   // ctx = *(kContextPtr) (= 0x4de5e0)            [RAM]
inline constexpr uint32_t kTopScreenPtr = 0x4fb60c; // current GUI screen object (box screens stack) [RAM]
inline constexpr uint32_t kStatsBlock = 0x4fbac8;   // S, the player stats block                   [RAM]
inline constexpr uint32_t kDecodeKey = 0x4fe86c;    // key for dec(x) = rotr32(x ^ key, 8)          [RAM]
inline constexpr uint32_t kQuestTable = 0x4fbb70;   // quest / map marker records                   [RAM]
inline constexpr uint32_t kQuestCount = 0x4fbb78;   //                                              [RAM]
inline constexpr uint32_t kRegionNames = 0x5126b8;  // region names, 0x20 bytes each                [RAM]
inline constexpr uint32_t kRegionStride = 0x20;
// The note says i < 14, but entry 13 holds unrelated text ("iStringTableSize") in every dump: 13 entries.
inline constexpr uint32_t kRegionCount = 13;

// Plausible heap / data pointer (the harness scripts' `0x100000 < p < 0x2000000`).
inline constexpr uint32_t kRamPtrMin = 0x100000;
inline constexpr uint32_t kRamPtrEnd = 0x2000000;

// ---- ctx = *(kContextPtr) ----
namespace ctx {
inline constexpr uint32_t kResourceDb = 0x320;   // res = *(ctx+0x320) (the note writes "ctx+0x320")
inline constexpr uint32_t kStringMgr = 0x344;    // *(ctx+0x344) = string manager (0x4dbca8)
inline constexpr uint32_t kActorRegistry = 0x7c; // handle = *(*(ctx+0x7c) + (ref-1)*8)
inline constexpr uint32_t kPlayerActor = 0x4b0;  // player actor handle; player = *(*(h+0xfc)+0x10)
} // namespace ctx

// ---- res = *(ctx + 0x320) ----
namespace res {
inline constexpr uint32_t kRecordTable = 0x8;
inline constexpr uint32_t kRecordCount = 0x10;
inline constexpr uint32_t kRecordSize = 0x24;
inline constexpr uint32_t kRecType = 0x4;
inline constexpr uint32_t kRecClass = 0x8;
inline constexpr uint32_t kRecProps = 0xc; // property block pointer {+4 data}
} // namespace res

// ---- property block {+4 data, +8 vtable}: prop(id) = *(data + id*4); prop(id,k) = *(data + (u16(data+id*4)+k)*4)
namespace props {
inline constexpr uint32_t kData = 0x4;
inline constexpr uint32_t kName = 0x0;        // string id                                  [RAM]
inline constexpr uint32_t kDropAllowed = 0xd; //                                            [dec] 33f2a0
inline constexpr uint32_t kPrice = 0x11;      // base price                                 [inf]
inline constexpr uint32_t kSkillNeeded = 0x13;
inline constexpr uint32_t kMaxA = 0x16;        // max quantity / durability; scroll: spell index + 1
inline constexpr uint32_t kMaxB = 0x17;        // scroll: spell level
inline constexpr uint32_t kMeleeDamage = 0x1a; // melee only                                 [inf]
inline constexpr uint32_t kSpellTypes = 0x1b6; // indexed, on the spell owner actor          [dec] 38f478
// quest resource properties
inline constexpr uint32_t kQuestStatus = 0x0; // 1 / 2 = listed in the journal
inline constexpr uint32_t kQuestOptional = 0x1;
inline constexpr uint32_t kQuestText = 0x2;
inline constexpr uint32_t kQuestCaption = 0x3;
} // namespace props

// ---- GUI screen object (*(kTopScreenPtr)) ----
namespace screen {
inline constexpr uint32_t kId = 0x18;
inline constexpr uint32_t kInventoryId = 0x15;
inline constexpr uint32_t kPlayer = 0x14c;       // player data object while 0x15 is the top screen
inline constexpr uint32_t kSection = 0x154;      // 0 Equipment, 1 Spells, 2 Skills, 3 Map, 4 Journal (ootf note)
inline constexpr uint32_t kAnimSpeed = 0x15c;    // float: open (> 0) / close (-2, 431d10) animation, 0 = idle (42fd18)
inline constexpr uint32_t kSections = 0x1a8;     // section object array (builder 42c9a0)            [RAM]
inline constexpr uint32_t kSectionCount = 0x1b0; // 5 in the inventory, 3 in a shop                [RAM]
} // namespace screen

// ---- section objects (*(screen+0x1a8))[i] ----
namespace section {
inline constexpr uint32_t kVtable = 0x18;
inline constexpr uint32_t kSkillsVtable = 0x54f1f0; // index 2 of the five-section inventory
inline constexpr uint32_t kSkillRow = 0x58;         // selected row 0..2 (Up / Down taps, wrap)   [RAM]
// While the Skills page is up, spending and returning (4468e0, from the key handler 445898) change only these working
// copies; leaving the page asks "apply?" and writes them to the stats block.                       [dec] [RAM]
inline constexpr uint32_t kSkillPending = 0x5c; // unspent points, working copy
inline constexpr uint32_t kSkillRows = 0xfc;    // row records, 0x1b4 bytes each, rows in skill-table order
inline constexpr uint32_t kSkillRowStride = 0x1b4;
inline constexpr uint32_t kRowLevel = 0x0;     // working level
inline constexpr uint32_t kRowCommitted = 0x4; // level when the page was entered (Left stops there)
// Index 3 of the five-section inventory is the map; its +0x70 is the region the map shows (data-model note
// section 4, [dec]; no vtable check known, so the value is range-checked against the region table).
inline constexpr uint32_t kMapSectionIndex = 3;
inline constexpr uint32_t kMapRegion = 0x70;
inline constexpr uint32_t kMapSelection = 0x6c; // selected record of the quest table; the arrows (43a588) cycle it
} // namespace section

// ---- item / actor handle ----
namespace actor {
inline constexpr uint32_t kMaxContinuation = 0x53; // slot values <= this are continuation markers
inline constexpr uint32_t kType = 0xc;             // 10 weapon, 11 usable
inline constexpr uint32_t kResIndex = 0x10;        // u16, 0xffff = none
inline constexpr uint32_t kProps = 0x54;           // property block; re-read, it moves
inline constexpr uint32_t kInstance = 0xfc;        // *(h+0xfc)+0x10 = instance data
inline constexpr uint32_t kInstanceData = 0x10;
inline constexpr uint32_t kQtyCur = 0x14; // dec()
inline constexpr uint32_t kQtyMax = 0x18; // dec()
inline constexpr uint32_t kTypeWeapon = 10;
inline constexpr uint32_t kTypeUsable = 11;
} // namespace actor

// ---- player data object ----
namespace player {
inline constexpr uint32_t kController = 0x584; // ctl
inline constexpr uint32_t kSpellList = 0x1024; // *(list) + 0x80 = spell owner handle
inline constexpr uint32_t kSpellOwner = 0x80;
inline constexpr uint32_t kSpellTable = 0x1028; // 0x14-byte entries
inline constexpr uint32_t kSpellCount = 0x1030;
inline constexpr uint32_t kSummonState = 0x1040; // 1..4 = summon running (spell selection refused)
inline constexpr uint32_t kSpellStride = 0x14;
inline constexpr uint32_t kSpellKnownLevel = 0x0; // 0 = not learned
inline constexpr uint32_t kSpellLevelRes = 0x4;
inline constexpr uint32_t kSpellState = 0x10;
inline constexpr uint32_t kMaxSpells = 32; // sanity bound (10 in every dump)
} // namespace player

// ---- inventory controller ctl = player + 0x584 ----
namespace ctl {
inline constexpr uint32_t kBusy = 0x0;
inline constexpr uint32_t kShopMode = 0xc;
inline constexpr uint32_t kShopkeeper = 0x18;
inline constexpr uint32_t kShopSelection = 0x1c;
inline constexpr uint32_t kSelection = 0x20; // cell 0..23, 0x1a / 0x1b, 0x1c = spell, -1 none
inline constexpr uint32_t kPane = 0x24;      // 0 Equipment, 1 Spells, 2 shop list
inline constexpr uint32_t kSlots = 0x30;
inline constexpr uint32_t kSlotStride = 0x10;
inline constexpr int kSlotCount = 24;
inline constexpr int kGridColumns = 8;
inline constexpr int kGridRows = 3;
inline constexpr uint32_t kSlot1A = 0x1c0;        // record 0x1a (not drawn)
inline constexpr uint32_t kSlotShopStock = 0x1d0; // record 0x1b: shop stock item while a shop is open
inline constexpr int kSelSlot1A = 0x1a;
inline constexpr int kSelShopStock = 0x1b;
inline constexpr int kSelSpell = 0x1c;
// formatted details text for the selection, 0x40 bytes each, in draw order
inline constexpr uint32_t kDetailLines[] = {0x244, 0x284, 0x2c4, 0x304, 0x344, 0x384, 0x3c4, 0x404, 0x450};
inline constexpr uint32_t kDetailLineSize = 0x40;
inline constexpr uint32_t kQuantity = 0x444;
inline constexpr uint32_t kAmmo = 0x448;
inline constexpr uint32_t kIconGlyph = 0x498;
inline constexpr uint32_t kRequirementMet = 0x49c;
inline constexpr uint32_t kSpellsHaveEntries = 0x4a8;
inline constexpr uint32_t kDetailsValid = 0x4ac;
inline constexpr uint32_t kSelectedSpell = 0x4b4;
} // namespace ctl

// ---- slot record (16 bytes) ----
namespace slot {
inline constexpr uint32_t kEquipped = 0x0;
inline constexpr uint32_t kHotSlot = 0x4;
inline constexpr uint32_t kItem = 0x8;
inline constexpr uint32_t kIconObject = 0xc; // only while 0x15 is open
} // namespace slot

// ---- stats block S ----
namespace stats {
inline constexpr uint32_t kSkillTable = 0x0;
inline constexpr uint32_t kSkillCount = 0x8;
inline constexpr uint32_t kSkillStride = 0x4c; // {id, level, max, name[...]} plain ints
inline constexpr uint32_t kSkillId = 0x0;
inline constexpr uint32_t kSkillLevel = 0x4;
inline constexpr uint32_t kSkillMax = 0x8;
inline constexpr uint32_t kSkillName = 0xc;
inline constexpr uint32_t kSkillNameMax = 0x40;
inline constexpr uint32_t kMaxSkills = 16; // sanity bound (3 in every dump)
// dec() values
inline constexpr uint32_t kHp = 0xc;
inline constexpr uint32_t kHpMax = 0x10;
inline constexpr uint32_t kGold = 0x18;
inline constexpr uint32_t kMana = 0x1c;
inline constexpr uint32_t kManaMax = 0x20;
inline constexpr uint32_t kXp = 0x24;
inline constexpr uint32_t kLevel = 0x28;
inline constexpr uint32_t kSkillPoints = 0x2c;
inline constexpr uint32_t kInventoryOpen = 0x34; // plain, 1 while screen 0x15 is open
} // namespace stats

// ---- quest / marker record (0x20 bytes) ----
namespace quest {
inline constexpr uint32_t kStride = 0x20;
inline constexpr uint32_t kMapX = 0x0;
inline constexpr uint32_t kMapY = 0x4;
inline constexpr uint32_t kIcon = 0xc;
inline constexpr uint32_t kEnabled = 0x10;
inline constexpr uint32_t kRegion = 0x14;
inline constexpr uint32_t kResIndex = 0x18;
inline constexpr uint32_t kMaxQuests = 256; // sanity bound
} // namespace quest

// ---- string manager: 128-bucket hash of resident strings ----
namespace strings {
inline constexpr uint32_t kBucketMask = 0x7f;
inline constexpr uint32_t kHashMul = 0x59;
inline constexpr uint32_t kBucketBaseWord = 8; // bucket = *(mgr + ((id*0x59 & 0x7f) + 8) * 4)
inline constexpr uint32_t kEntryId = 0x0;
inline constexpr uint32_t kEntryFlags = 0x8; // & 3 == 3: loaded
inline constexpr uint32_t kEntryNext = 0x14;
inline constexpr uint32_t kEntryText = 0x1c;
inline constexpr uint32_t kLoadedMask = 3;
inline constexpr uint32_t kMaxChain = 64;
inline constexpr uint32_t kMaxText = 1024;
} // namespace strings

// ---- resource classes (rec + 8) ----
namespace cls {
inline constexpr uint32_t kMelee = 0x1000e;
inline constexpr uint32_t kBow = 0x1004f;
inline constexpr uint32_t kArmourA = 0x1002d; // needs Exp. Level: armour / shield [inf]
inline constexpr uint32_t kArmourB = 0x10090;
inline constexpr uint32_t kArmourC = 0x100c7;
inline constexpr uint32_t kHealthElixir = 0x1000a;
inline constexpr uint32_t kUsableB = 0x10059;
inline constexpr uint32_t kUsableC = 0x100ab;
inline constexpr uint32_t kSpellScroll = 0x100b2;
inline constexpr uint32_t kAmmoA = 0x10027; // own branches in 342f90 [unknown]
inline constexpr uint32_t kAmmoB = 0x1005f;
inline constexpr uint32_t kDragon = 0x1001c; // ctx+0x4b0 class while riding Arokh
} // namespace cls

// ---- native options object (0x4fb6d0): the modern pause menu's Audio / Controller values ----
// docs/journal/2026-10-25/modern-pause-menu.md section 1.2; the same s32 fields drakan_menus.cpp persists. Read only:
// changes go through the native widgets (see PauseActionKind and the README).
namespace options {
inline constexpr uint32_t kObject = 0x4fb6d0;
inline constexpr uint32_t kReversePitch = 0x4fb734;     // +0x64 "FlipYAxis", 0 / 1, widget 0x2369     [dec]
inline constexpr uint32_t kVibration = 0x4fb73c;        // +0x6c "Vibrate", 0 / 1, widget 0x2368       [dec]
inline constexpr uint32_t kSubtitles = 0x4fb740;        // +0x70, 0 / 1, widget 0x2353                 [dec]
inline constexpr uint32_t kStereo = 0x4fb744;           // +0x74, 0 / 1, widget 0x2354                 [dec]
inline constexpr uint32_t kMasterVolume = 0x4fb75c;     // +0x8c "SoundVolume", 0..100, widget 0x2351  [dec]
inline constexpr uint32_t kMusicVolume = 0x4fb760;      // +0x90 "MusicVolume", 0..100, widget 0x2352  [dec]
inline constexpr uint32_t kControllerScheme = 0x4fb9b8; // +0x2e8 "JoyConfig_Scheme", 0..2 = A / B / C [dec]
inline constexpr int32_t kVolumeMin = 0;
inline constexpr int32_t kVolumeMax = 100;
inline constexpr int32_t kSchemeCount = 3;
} // namespace options

// ---- native GUI screens, widgets and functions the pause menu's actions drive (used by the glue) ----
namespace gui {
inline constexpr uint32_t kScreenTable = 0x4fb610; // screen object by id = *(kScreenTable + 4 * id)    [dec]
inline constexpr uint32_t kScreenPause = 0x17;     // layout 0x2310, handler 0x431f00
inline constexpr uint32_t kScreenOptions = 9;      // layout 0x2340, handler 0x4424e0
inline constexpr uint32_t kScreenController = 10;  // layout 0x2360, handler 0x441728
inline constexpr uint32_t kScreenAudio = 13;       // layout 0x2350, handler 0x441258 (apply 0x441440)
inline constexpr uint32_t kScreenTitle = 8;
inline constexpr uint32_t kScreenBox = 1; // Yes / No box, layout 0x2380; the answer is message 0x10 to the parent
// pause rows (layout 0x2310)
inline constexpr uint32_t kPauseSave = 0x2311;
inline constexpr uint32_t kPauseLoad = 0x2312;
inline constexpr uint32_t kPauseOptions = 0x2313;
inline constexpr uint32_t kPauseResume = 0x2314;
inline constexpr uint32_t kPauseQuit = 0x2315; // Yes / No box, property 0x14a
// Options rows (layout 0x2340)
inline constexpr uint32_t kOptionsController = 0x2341;
inline constexpr uint32_t kOptionsAudio = 0x2342;
inline constexpr uint32_t kOptionsDefaults = 0x2344; // Yes / No box, property 0x156
inline constexpr uint32_t kOptionsPcSettings = 0x2345;
inline constexpr uint32_t kOptionsBack = 0x2346;
// Audio widgets (layout 0x2350)
inline constexpr uint32_t kAudioMaster = 0x2351;    // slider (screen+0x9c); apply: sound mgr vt+0xa4(value, -1)
inline constexpr uint32_t kAudioMusic = 0x2352;     // slider (screen+0xa0); apply: vt+0xa4(value, 2)
inline constexpr uint32_t kAudioSubtitles = 0x2353; // check box (flag 0x80); no apply
inline constexpr uint32_t kAudioStereo = 0x2354;    // check box; apply: vt+0xc4()
// Controller widgets (layout 0x2360)
inline constexpr uint32_t kControllerScheme = 0x2367; // button: cycles the scheme 0 -> 1 -> 2 -> 0
inline constexpr uint32_t kControllerVibration = 0x2368;
inline constexpr uint32_t kControllerReversePitch = 0x2369;
// box answers (layout 0x2380)
inline constexpr uint32_t kBoxYes = 0x238b;
inline constexpr uint32_t kBoxNo = 0x238d;
// slider widget fields and the functions
inline constexpr uint32_t kSliderMin = 0x7c;   // float                                              [dec]
inline constexpr uint32_t kSliderMax = 0x80;   // float
inline constexpr uint32_t kSliderValue = 0x84; // float
inline constexpr uint32_t kFnSliderGet = 0x2c6c78;
inline constexpr uint32_t kFnSliderSet = 0x2c6c80;   // (float v, widget): clamp to +0x7c..+0x80, mark dirty, no calls
inline constexpr uint32_t kFnPostMessage = 0x2bcf80; // (screen, widget, msg): message 1 = the widget's activation
} // namespace gui

// Load (4) / Save (5) slot screens: docs/journal/2026-10-25/modern-save-load-title.md section 1, save-slots.md.
// [dec] = read in the decompile (build/recomp_ui/alldecomp), [L] = verified live in earlier notes, [U] = inferred.
namespace saveui {
inline constexpr uint32_t kScreenLoad = 4;              // class ctor 43cf38, handler 43ac40, layout 0x2330  [dec]
inline constexpr uint32_t kScreenSave = 5;              // ctor 4442e0 (subclass), handler 443360          [dec]
inline constexpr uint32_t kScreenSaving = 0x1a;         // "Saving..." screen opened by the write 443ac0   [dec]
inline constexpr uint32_t kScreenLoading = 0xe;         // loading screen (New Game, a load)               [L]
inline constexpr uint32_t kFlags = 0x9c;                // u16: 2 box up, 4 / 8 rebuild the list, 0x10 load confirmed
inline constexpr uint32_t kBoxState = 0xa0;             // 1 overwrite, 2 error / no card, 4 card changed, 5 load ask
inline constexpr uint32_t kSelection = 0xa4;            // selected row 0..3 on the page                  [L]
inline constexpr uint32_t kEntries = 0xb8;              // entry array, kEntrySize each                     [L]
inline constexpr uint32_t kCount = 0xc0;                // entries (4 once the scan ran, 0 before)          [L]
inline constexpr uint32_t kSaveMode = 0x1e4;            // save only: 1 = the save offered at a level change [L]
inline constexpr uint32_t kEntrySize = 0x168;           //                                                  [L]
inline constexpr uint32_t kEntryPath = 0x0;             // char[256] "BESCES-50006SlotN/<level>.rsg"; "" = no save
inline constexpr uint32_t kEntryCaption = 0x100;        // char[40]: level caption, or the empty / no-space text [dec]
inline constexpr uint32_t kEntryCaptionMax = 0x27;      // +0x127 is forced to 0
inline constexpr uint32_t kEntrySortKey = 0x128;        // s64: 0x7fffffffffffffff = no save in the slot   [dec]
inline constexpr uint32_t kEntryTime = 0x130;           // 6 x s32: second, minute, hour, day, month, year  [dec]/[U]
inline constexpr uint32_t kEntryThumbRequested = 0x154; // 1 once the grid draw asked for the thumbnail   [dec]
inline constexpr uint32_t kEntryX = 0x15c, kEntryY = 0x160; // GUI units (512 x 512): the native card origin [L]
inline constexpr uint32_t kEntryThumb = 0x164;              // thumbnail image object (+0x24 w, +0x28 h, +0x50 CT16) [L]
inline constexpr int kSlotsPerPage = 4;
inline constexpr int64_t kNoSaveKey = 0x7fffffffffffffffLL;
inline constexpr uint32_t kSlotNamePtrs = 0x4fe3b8; // char *[4] -> "Slot1".."Slot4" (the pager rewrites them) [L]
inline constexpr uint32_t kWidgetGrid = 0x2331;     // image widget: its draw sends message 10 -> grid draw 43bfb0
inline constexpr uint32_t kWidgetReading = 0x2332;  // label shown while the card is missing
inline constexpr uint32_t kWidgetTitle = 0x2333;    // the screen's title label
inline constexpr uint32_t kFnKey = 0x43b1e8;        // key handler (+0x50 key id: 0x1c Enter, 1 Esc, arrows) [L]
inline constexpr uint32_t kFnScan = 0x43b6f0;       // directory scan of the four slot folders            [dec]
inline constexpr uint32_t kFnGridDraw = 0x43bfb0;   // draws the 2 x 2 grid, requests the thumbnails      [dec]
inline constexpr uint32_t kFnLoadAsk = 0x43d028;    // Load message 2: ask in game (box 0x107) or load     [dec]
inline constexpr uint32_t kFnLoadStart = 0x43b4d0;  // starts the load of the selected entry              [dec]
inline constexpr uint32_t kFnSaveSelect = 0x444460; // Save message 2: empty -> write, used -> overwrite box [dec]
inline constexpr uint32_t kFnSaveWrite = 0x443ac0;  // writes the selected slot (Saving screen 0x1a)      [dec]
inline constexpr uint32_t kFnAnySave = 0x37e810;    // (0): 0 card / no save, 1 save, 2 no card, 3 unformatted, 4 full
// INTERFCE.ODB property ids used by these screens (texts through the string manager).
inline constexpr uint32_t kPropEmptyCaption = 0xfa;   // caption of an empty slot                         [dec]
inline constexpr uint32_t kPropNoSpaceCaption = 0xfb; // caption of a slot that has no room for a new save [dec]
inline constexpr uint32_t kPropNoSpaceText = 0xfc;    // the explanation drawn in that card               [dec]
inline constexpr uint32_t kPropOverwrite = 0x100;     // overwrite question ("%d - %s" inserted)          [dec]
inline constexpr uint32_t kPropLoadInGame = 0x107;    // load while a level runs: Yes / No, default No    [dec]
inline constexpr uint32_t kPropNoCard = 0x108;        // no memory card: OK box                           [dec]
inline constexpr uint32_t kPropCardChanged = 0x109;   // the card changed since the list was read: OK box [dec]
} // namespace saveui

// Title menu (screen 8), layout 0x2300, handler 43e4c8 (settings-menu.md 2.3, save-slots.md 3).
namespace title {
inline constexpr uint32_t kScreenTitle = 8;
inline constexpr uint32_t kScreenCredits = 0x13;     // widget-less page, Cross continues                [L]
inline constexpr uint32_t kScreenMemcardWarn = 0x1c; // "no / empty memory card" warning before the logo   [dec]
inline constexpr uint32_t kScreenIntroMovie = 0x1d;  //                                                     [L]
inline constexpr uint32_t kScreenLogoMovie = 0x1e;   //                                                     [L]
inline constexpr uint32_t kWidgetArt = 0x2301;       // image widget sized to the whole GUI: the title art  [dec]
inline constexpr uint32_t kWidgetLoad = 0x2302;      // -> screen 4 (parent title); enabled iff 37e810(0) == 1
inline constexpr uint32_t kWidgetQuit = 0x2303;      // hidden by the stock handler; re-enabled by drakan_menus
inline constexpr uint32_t kWidgetNew = 0x2304;       // first level name (property 0x3f) -> screen 0xe
inline constexpr uint32_t kWidgetOptions = 0x2305;   // -> screen 9 (parent title)
inline constexpr uint32_t kWidgetCredits = 0x2306;   // -> screen 0x13
inline constexpr uint32_t kRowTable = 0x54e768;      // row order (ids): Load, New, Options, Credits, Quit   [dec]
inline constexpr uint32_t kFnHandler = 0x43e4c8;
inline constexpr uint32_t kFnEnableLoad = 0x43f398; // Load Game enable state from 37e810(0)              [dec]
inline constexpr uint32_t kPropMusic = 0x3a;        // title music, started on channel 2 by message 3      [dec]
inline constexpr uint32_t kPropFirstLevel = 0x3f;   // New Game's level name                               [dec]
} // namespace title

} // namespace gameui::addr
