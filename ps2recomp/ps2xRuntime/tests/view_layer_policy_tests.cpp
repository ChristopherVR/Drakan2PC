// drakan_view_layer: the layer the view is drawn from when the follow camera's point has none (issue #6 item 1).
// The numbers are the Surdana spider cellar on the PAL disc: 4 is the room at the head of the stairs (layer 453),
// 5 the corridor below it, 195 the outdoor layer above both, which lists neither.
#include "../src/lib/drakan_view_layer.h"
#include <cassert>
#include <iostream>

using namespace drakan_view_layer;

namespace {

Lookup lookup(int32_t oldLayer, int32_t native, int32_t actorLayer, bool nearActor, bool related) {
    Lookup l;
    l.oldLayer = oldLayer;
    l.native = native;
    l.actorLayer = actorLayer;
    l.cameraNearActor = nearActor;
    l.nativeRelated = related;
    return l;
}

} // namespace

int main() {
    // The reported fault, frame 1: the camera is carried 5 units past the east wall of the room; the lookup from
    // the room finds no listed layer there. The view is answered from Rynn's layer: nothing listed holds the point,
    // so it is her layer itself.
    {
        const Lookup l = lookup(4, kNoLayer, 4, true, false);
        assert(decide(l) == Action::FromActor);
        assert(resolve(Action::FromActor, l, kNoLayer) == 4);
        // ...or the listed layer that does hold the point, when the lookup from her layer finds one.
        assert(resolve(Action::FromActor, l, 5) == 5);
    }
    // Frame 2 without the rule: the view has no layer, the search over all layers returns the outdoor layer 195,
    // which is unrelated to her layer. Refused the same way (a view that reaches -1 by any other path).
    {
        const Lookup l = lookup(kNoLayer, 195, 4, true, false);
        assert(decide(l) == Action::FromActor);
        assert(resolve(Action::FromActor, l, kNoLayer) == 4);
    }
    // A search over all layers that ends in a layer related to hers stands (a level start, a cut back to her).
    {
        const Lookup l = lookup(kNoLayer, 5, 4, true, true);
        assert(decide(l) == Action::Native);
        assert(resolve(Action::Native, l, kNoLayer) == 5);
    }
    // Ordinary frames: the lookup finds a layer; whatever it is, the engine's result stands.
    assert(decide(lookup(4, 5, 4, true, true)) == Action::Native);
    assert(decide(lookup(4, 4, 4, true, true)) == Action::Native);
    assert(decide(lookup(205, 259, 205, true, true)) == Action::Native);
    assert(decide(lookup(195, 195, 195, true, true)) == Action::Native);
    // A lookup from a known layer that finds an unrelated one is the engine's business (only -1 and the search over
    // all layers are answered).
    assert(decide(lookup(11, 272, 11, true, false)) == Action::Native);
    // Rynn between layers for a tick (the engine's own handling at walls): the view keeps its layer instead of
    // starting the search over all layers; with a result, or with no old layer, the engine's result stands.
    {
        const Lookup l = lookup(4, kNoLayer, kNoLayer, true, false);
        assert(decide(l) == Action::KeepOld);
        assert(resolve(Action::KeepOld, l, kNoLayer) == 4);
        assert(decide(lookup(4, 5, kNoLayer, true, false)) == Action::Native);
        assert(decide(lookup(kNoLayer, 195, kNoLayer, true, false)) == Action::Native);
        assert(decide(lookup(kNoLayer, kNoLayer, kNoLayer, true, false)) == Action::Native);
    }
    // A camera that is not following her (a placement in flight, a cut scene elsewhere, a level arrival): the
    // engine's handling in every case, also the ones answered above.
    assert(decide(lookup(4, kNoLayer, 4, false, false)) == Action::Native);
    assert(decide(lookup(kNoLayer, 195, 4, false, false)) == Action::Native);
    assert(decide(lookup(4, kNoLayer, kNoLayer, false, false)) == Action::Native);
    assert(resolve(Action::Native, lookup(4, kNoLayer, 4, false, false), 5) == kNoLayer);
    // The range that counts as following: the camera 1500 units behind her is near (2200 with the travel lag); one
    // parked 3900 units off after a placement, Arokh's at 4400 and one 5000 units off are not.
    assert(nearActor(1200.0f, 400.0f, 800.0f));
    assert(nearActor(0.0f, 0.0f, kFollowRange));
    assert(nearActor(2100.0f, 300.0f, 600.0f));
    assert(!nearActor(3800.0f, 400.0f, 800.0f));
    assert(!nearActor(4300.0f, 900.0f, 0.0f));
    assert(!nearActor(3000.0f, 0.0f, 4000.0f));
    assert(!nearActor(0.0f, 20000.0f, 0.0f)); // straight above the cellar on the outdoor terrain
    // A placed actor: more than 2048 units between two view updates. Running (about 30 units a frame), a fall and
    // Arokh at full speed are far below it.
    assert(!teleported(30.0f, -80.0f, 20.0f));
    assert(!teleported(600.0f, 0.0f, 600.0f));
    assert(teleported(0.0f, 16000.0f, 0.0f)); // from the cellar onto the terrain above it
    assert(teleported(-190000.0f, 44000.0f, -199000.0f));
    // After a placement the rule stands aside for kSettleUpdates view updates, then acts again; a second placement
    // in that time starts the count again; a new level or a new controlled actor counts as a placement.
    {
        Settle s;
        assert(s.update(false)); // the first frames of a level
        for (int i = 0; i < kSettleUpdates; ++i)
            s.update(false);
        assert(!s.update(false));
        assert(s.update(true));
        for (int i = 0; i < kSettleUpdates - 2; ++i)
            assert(s.update(false));
        assert(s.update(true)); // placed again shortly before the end
        for (int i = 0; i < kSettleUpdates - 1; ++i)
            assert(s.update(false));
        assert(!s.update(false));
        assert(!s.update(false));
    }
    std::cout << "view_layer_policy_tests: ok\n";
    return 0;
}
