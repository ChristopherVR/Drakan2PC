// The "M" map view's objectives overlay. The map image is drawn natively (full screen); this screen draws only the
// objectives panel docked at the viewport's right edge and the highlight ring on the selected marker, and handles
// navigation. Order of the Flame letters the objectives on the parchment and strikes completed ones through; here
// they are a modern panel in the Tab screen's style: the location, the main and optional objectives (completed ones
// struck through) and the places marked on the map.
//
// Objectives and markers are linked both ways through their quest record: focusing a row rings its marker on the
// map, focusing a marker (pointer, or Left from the list) highlights its row. The glue passes the markers' screen
// positions (it knows where it drew the map); markerToScreen() converts a record's map-image position.
#pragma once
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/Model.h"
#include "gameui/Screen.h"

namespace gameui {

struct MapMarker {
    int quest = -1;     // QuestEntry::index of the record that owns the marker
    float x = 0, y = 0; // marker centre in back-buffer pixels
};

struct MapInput {
    std::vector<MapMarker> markers; // the markers drawn on the visible map (others get no ring / target)
    std::string location;           // optional: the level / location name when the glue knows it
};

class MapScreen {
public:
    explicit MapScreen(ScreenConfig cfg = {});

    // `quest` = the record to focus first (-1: the first open main objective).
    void open(const Snapshot& s, int quest = -1);
    void close();
    bool isOpen() const { return open_; }

    // One UI frame. Back / Close (and right click) set wantsClose (no UiAction: the glue closes the native view).
    FrameOutput frame(const Snapshot& s, const MapInput& m, const UiInput& in, const Viewport& vp);

    // Quest record of the focused row or marker, -1 when nothing is focused.
    int selectedQuest() const { return focus_.kind == TargetKind::None ? -1 : focus_.index; }
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }
    // The panel in pixels (last frame): the glue may fit the map image left of it.
    Rect panelRect() const { return panelPx_; }
    static Rect panelRectFor(const Viewport& vp);

    // Screen position of a record's marker when the map image (imageW x imageH pixels) is drawn into `image`.
    static Vec2 markerToScreen(const QuestEntry& q, const Rect& image, float imageW, float imageH);

private:
    struct Row {
        enum Kind : uint8_t { Heading, Objective, Place } kind = Heading;
        int quest = -1;
        std::string label;
        std::vector<std::string> lines;
        bool done = false;
        bool optional = false;
        float y = 0, h = 0; // design units, content coordinates (before scrolling)
    };

    ScreenConfig cfg_;
    bool open_ = false;
    int initialQuest_ = -1;
    int lastMarkerSteer_ = -1; // record of the last SelectMarker sent (the native selection follows the focus)
    TargetId focus_, hover_;
    float scroll_ = 0;
    std::vector<Row> rows_;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    std::vector<Rect> hintRects_;
    std::vector<MapMarker> markers_;
    std::string location_;
    LayoutFrame lf_;
    Rect panelPx_;

    const HitTarget* findTarget(const TargetId& id) const;
    const MapMarker* markerFor(int quest) const;
    const Row* rowFor(int quest) const;
    void buildRows(const Snapshot& s);
    void buildTargets();
    void ensureFocus();
    void buildHints();
    void applyInput(const UiInput& in, FrameOutput& out);
    void press(Prompt p, FrameOutput& out);
    void draw(const Snapshot& s, DrawList& d) const;
};

} // namespace gameui
