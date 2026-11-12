// The renderer a session starts on after the previous one lost its device or died (gsr/renderer_session.h).
#include "gsr/renderer_session.h"

#include <cstdio>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    using gsr::Config;
    using namespace gsr::session;
    const auto WebGpu = Config::Api::WebGpu;
    const auto D3D11 = Config::Api::D3D11;
    {
        const Decision d = decide(Record{}, WebGpu, true);
        expect(d.api == WebGpu && !d.changed && d.notice.empty(), "no record: the player's choice");
    }
    {
        Record lost;
        lost.api = WebGpu;
        lost.end = End::DeviceLost;
        lost.reason = "driver reset";
        const Decision d = decide(lost, WebGpu, true);
        expect(d.api == D3D11 && d.changed, "device lost: Direct3D 11 this once");
        expect(d.notice.find("driver reset") != std::string::npos, "the notice carries the driver's reason");
        const Decision linux_ = decide(lost, WebGpu, false);
        expect(linux_.api == WebGpu && !linux_.changed && !linux_.notice.empty(),
               "device lost without Direct3D 11: same renderer, with a notice");
        const Decision asked = decide(lost, D3D11, true);
        expect(asked.api == D3D11 && !asked.changed && asked.notice.empty(), "the player chose Direct3D 11 anyway");
    }
    {
        // One unexplained end changes nothing; the second in a row does.
        Record first = begin(Record{}, WebGpu); // session 1 starts, never ends cleanly
        expect(first.end == End::Running && first.abnormalInRow == 1, "a started session is 'running'");
        expect(decide(first, WebGpu, true).api == WebGpu, "one unexplained end: the player's choice");
        Record second = begin(first, WebGpu); // session 2 starts, dies too
        expect(second.abnormalInRow == 2, "two in a row are counted");
        const Decision d = decide(second, WebGpu, true);
        expect(d.api == D3D11 && d.changed && !d.notice.empty(), "two unexplained ends in a row: Direct3D 11");
        // The fallback session runs on D3D11 and ends cleanly: the count starts again.
        Record fallback = begin(second, D3D11);
        fallback.end = End::Clean;
        expect(decide(fallback, WebGpu, true).api == WebGpu, "after the fallback session the choice is tried again");
        Record third = begin(fallback, WebGpu);
        expect(third.abnormalInRow == 1, "a different renderer in between resets the count");
    }
    {
        Record clean = begin(Record{}, WebGpu);
        clean.end = End::Clean;
        Record next = begin(clean, WebGpu);
        expect(next.abnormalInRow == 1, "a clean end resets the count");
        expect(decide(clean, WebGpu, true).api == WebGpu, "clean end: the player's choice");
    }
    {
        Record d3d;
        d3d.api = D3D11;
        d3d.end = End::Running;
        d3d.abnormalInRow = 5;
        expect(decide(d3d, WebGpu, true).api == WebGpu, "ends on Direct3D 11 say nothing about the shared renderer");
    }
    {
        Record r;
        r.api = WebGpu;
        r.end = End::DeviceLost;
        r.abnormalInRow = 3;
        r.reason = "line one\nline two = x";
        const Record back = parse(serialize(r));
        expect(back.api == WebGpu && back.end == End::DeviceLost && back.abnormalInRow == 3, "round trip");
        expect(back.reason == "line one line two = x", "the reason stays on one line");
        expect(parse("garbage\n\n=\nend=clean").end == End::Clean, "unknown lines are skipped");
        expect(parse("").end == End::None && load("no/such/file").end == End::None, "missing record");
    }
    {
        // A session that died inside the graphics library says so in its log, not in its record.
        Record died = begin(Record{}, WebGpu);
        const Record plain = interpret(died, "[gsr] exit: frames=10\n");
        expect(plain.end == End::Running, "a log without a loss leaves the record as it is");
        const Record lost =
            interpret(died, "...\nError in wgpuSurfaceGetCurrentTexture: Validation Error\n\nCaused by:\n"
                            "  Parent device is lost\n");
        expect(lost.end == End::DeviceLost && !lost.reason.empty(), "the abort's last words mark a device loss");
        expect(decide(lost, WebGpu, true).api == D3D11, "and the next start begins on Direct3D 11");
        Record clean = died;
        clean.end = End::Clean;
        expect(interpret(clean, "Parent device is lost").end == End::Clean, "a clean end is not reinterpreted");
        expect(tail("no/such/file").empty(), "a missing log reads as empty");
    }
    {
        // A loss during play: the record counts losses in a row on one renderer, the next start is unchanged on
        // Direct3D 11, and the notice is plain words with what to try.
        Record first = lost(begin(Record{}, D3D11), "the graphics driver was reset");
        expect(first.end == End::DeviceLost && first.lostInRow == 1, "a loss is recorded and counted");
        const Decision next = decide(first, D3D11, true);
        expect(next.api == D3D11 && !next.changed && next.notice.empty(),
               "a loss on Direct3D 11 does not change the next start");
        expect(decide(first, WebGpu, true).api == WebGpu, "nor a start on the other renderer");
        expect(!lossFallsBack(D3D11, true) && lossFallsBack(WebGpu, true) && !lossFallsBack(WebGpu, false),
               "only a loss on the shared renderer falls back, and only where Direct3D 11 exists");
        const std::string once = lossNotice(first, first.reason, lossFallsBack(D3D11, true));
        expect(once.find("the graphics driver was reset") != std::string::npos, "the notice says what happened");
        expect(once.find("will close") != std::string::npos, "and that the game closes");
        expect(once.find("last save") != std::string::npos, "and that unsaved progress is lost");
        expect(once.find("update your graphics driver") != std::string::npos &&
                   once.find("Internal render scale") != std::string::npos &&
                   once.find("ray-traced") != std::string::npos,
               "and what to try");
        expect(once.find("in a row") == std::string::npos && once.find("next start") == std::string::npos,
               "a first loss on Direct3D 11 mentions neither a repeat nor another renderer");
        const Record second = lost(begin(first, D3D11), "the graphics driver was reset");
        expect(second.lostInRow == 2, "a second loss in a row is counted");
        const std::string twice = lossNotice(second, second.reason, false);
        expect(twice.find("2 sessions in a row") != std::string::npos, "the second notice says it happened again");
        Record clean = begin(second, D3D11);
        clean.end = End::Clean;
        expect(begin(clean, D3D11).lostInRow == 0, "a clean session ends the run of losses");
        expect(begin(first, WebGpu).lostInRow == 0, "a loss on another renderer is not counted");
        const Record back = parse(serialize(second));
        expect(back.lostInRow == 2 && back.end == End::DeviceLost, "the count survives the record");
        expect(parse("renderer=d3d11\nend=clean\nabnormal=0\nreason=\n").lostInRow == 0,
               "a record written before the count reads as no losses");
        // WebGPU: the library's text stays in the log, the player reads plain words and the fallback.
        const Record web = lost(begin(Record{}, WebGpu), "Parent device is lost");
        const std::string webText = lossNotice(web, web.reason, lossFallsBack(WebGpu, true));
        expect(webText.find("Parent device") == std::string::npos, "library text is not shown to the player");
        expect(webText.find("Direct3D 11 renderer once") != std::string::npos, "the fallback is announced");
        for (const std::string* t : {&once, &twice, &webText}) {
            expect(t->find("\xE2\x80\x94") == std::string::npos, "no em-dash in the notice");
            expect(t->find("DXGI") == std::string::npos && t->find("device") == std::string::npos,
                   "no API terms in the notice");
        }
        expect(std::string(lossNoticeTitle()).find("\xE2\x80\x94") == std::string::npos, "no em-dash in the title");
    }
    std::printf("%s\n", g_failures ? "renderer session: FAILED" : "renderer session: ok");
    return g_failures ? 1 : 0;
}
