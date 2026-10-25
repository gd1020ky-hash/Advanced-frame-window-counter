#include "App.hpp"
#include <Geode/ui/Notification.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

namespace afwc {

App& app() {
    static App a;
    return a;
}

void notify(std::string const& msg, bool error) {
    Notification::create(msg, error ? NotificationIcon::Error : NotificationIcon::Success, 2.5f)->show();
}

// ------------------------------------------------------------------------------------------------------------------
// settings
// ------------------------------------------------------------------------------------------------------------------
ScanConfig scanConfigFromSettings() {
    ScanConfig c;
    c.maxShift = int(opt<int64_t>("scan-max-shift"));
    c.horizonExtra = int(opt<int64_t>("scan-horizon"));
    c.shiftMode = opt<std::string>("scan-shift-mode") == "press-only" ? ShiftMode::PressOnly : ShiftMode::WholeTap;
    c.verifyBaseline = opt<bool>("scan-verify");
    return c;
}

// ------------------------------------------------------------------------------------------------------------------
// paths
// ------------------------------------------------------------------------------------------------------------------
static std::filesystem::path ensureDir(std::filesystem::path p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}
std::filesystem::path runsDir() { return ensureDir(Mod::get()->getSaveDir() / "runs"); }

// ------------------------------------------------------------------------------------------------------------------
// run (de)serialisation  --  "afwc-run" JSON, version 1
// ------------------------------------------------------------------------------------------------------------------
static matjson::Value runToJson(Run const& r) {
    matjson::Value root = matjson::Value::object();
    root["format"] = "afwc-run";
    root["version"] = 1;
    root["levelID"] = int(r.levelID);
    root["levelName"] = r.levelName;
    root["tps"] = r.tps;
    root["endTick"] = int(r.endTick);
    root["completed"] = r.completed;
    root["outcome"] = int(r.outcome);
    root["replayCheck"] = int(r.replayCheck);
    root["alignShift"] = int(r.alignShift);
    root["scannable"] = r.scannable;
    root["source"] = r.source;
    matjson::Value ev = matjson::Value::array();
    for (auto const& e : r.events) {
        matjson::Value row = matjson::Value::array();
        row.push(int(e.tick));
        row.push(e.down ? 1 : 0);
        row.push(e.p2 ? 1 : 0);
        ev.push(row);
    }
    root["events"] = ev;
    matjson::Value cl = matjson::Value::array();
    for (auto const& c : r.clicks) {
        matjson::Value o = matjson::Value::object();
        o["t"] = int(c.tick);
        o["r"] = int(c.release);
        o["p2"] = c.p2;
        o["if"] = int(c.ifCount);
        o["draw"] = c.shouldDraw;
        o["e"] = int(c.early);
        o["l"] = int(c.late);
        o["f"] = int(c.flags);
        o["mw"] = c.manualWindow;
        o["x"] = double(c.x);
        o["y"] = double(c.y);
        o["pc"] = double(c.percent);
        o["m"] = int(c.mode);
        cl.push(o);
    }
    root["clicks"] = cl;
    return root;
}

static bool runFromJson(matjson::Value const& root, Run& out, std::string* err) {
    if (!root.isObject() || root["format"].asString().unwrapOr("") != "afwc-run") {
        if (err) *err = "not an AFWC run file";
        return false;
    }
    Run r;
    r.levelID = int(root["levelID"].asInt().unwrapOr(0));
    r.levelName = root["levelName"].asString().unwrapOr("");
    r.tps = root["tps"].asDouble().unwrapOr(240.0);
    if (!(r.tps > 0)) r.tps = 240.0;
    r.endTick = int(root["endTick"].asInt().unwrapOr(0));
    r.completed = root["completed"].asBool().unwrapOr(false);
    r.outcome = uint8_t(std::clamp<int64_t>(root["outcome"].asInt().unwrapOr(0), 0, 2));
    r.replayCheck = uint8_t(std::clamp<int64_t>(root["replayCheck"].asInt().unwrapOr(0), 0, 2));
    r.alignShift = int(root["alignShift"].asInt().unwrapOr(0));
    r.scannable = root["scannable"].asBool().unwrapOr(true);
    r.source = root["source"].asString().unwrapOr("afwc");
    if (root.contains("events") && root["events"].isArray()) {
        for (auto const& row : root["events"].asArray().unwrap()) {
            if (!row.isArray() || row.size() < 3) continue;
            InputEvent e;
            e.tick = int(row[0].asInt().unwrapOr(0));
            e.down = row[1].asInt().unwrapOr(1) != 0;
            e.p2 = row[2].asInt().unwrapOr(0) != 0;
            r.events.push_back(e);
        }
    }
    if (root.contains("clicks") && root["clicks"].isArray()) {
        for (auto const& o : root["clicks"].asArray().unwrap()) {
            Click c;
            c.tick = int(o["t"].asInt().unwrapOr(0));
            c.release = int(o["r"].asInt().unwrapOr(-1));
            c.p2 = o["p2"].asBool().unwrapOr(false);
            c.ifCount = std::max(1, int(o["if"].asInt().unwrapOr(1)));
            c.shouldDraw = o["draw"].asBool().unwrapOr(true);
            c.early = int(o["e"].asInt().unwrapOr(-1));
            c.late = int(o["l"].asInt().unwrapOr(-1));
            c.flags = uint8_t(o["f"].asInt().unwrapOr(0));
            c.manualWindow = o["mw"].asDouble().unwrapOr(0.0);
            c.x = float(o["x"].asDouble().unwrapOr(0));
            c.y = float(o["y"].asDouble().unwrapOr(0));
            c.percent = float(o["pc"].asDouble().unwrapOr(0));
            int m = int(o["m"].asInt().unwrapOr(int(Mode::Unknown)));
            c.mode = (m >= 0 && m <= int(Mode::Unknown)) ? Mode(m) : Mode::Unknown;
            r.clicks.push_back(c);
        }
    }
    sortEvents(r.events);
    std::stable_sort(r.clicks.begin(), r.clicks.end(), [](Click const& a, Click const& b) { return a.tick < b.tick; });
    if (r.clicks.empty() && !r.events.empty()) deriveClicks(r);
    out = std::move(r);
    return true;
}

bool saveRunFile(Run const& r, std::filesystem::path const& path, std::string* err) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open file for writing"; return false; }
    f << runToJson(r).dump(0);
    return bool(f);
}

bool loadRunFile(std::filesystem::path const& path, Run& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open file"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    auto parsed = matjson::Value::parse(ss.str());
    if (parsed.isErr()) { if (err) *err = "invalid JSON"; return false; }
    return runFromJson(parsed.unwrap(), out, err);
}

static std::filesystem::path runPathFor(int levelID) {
    return runsDir() / fmt::format("level_{}.afwc.json", levelID);
}

bool loadRunForLevel(int levelID) {
    auto p = runPathFor(levelID);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return false;
    Run r;
    if (!loadRunFile(p, r, nullptr)) return false;
    setActiveRun(std::move(r), false);
    return true;
}

std::string describeRun(Run const& r) {
    int n = int(r.clicks.size()), withW = 0, bad = 0;
    for (auto const& c : r.clicks) {
        if (c.hasWindow()) withW++;
        if (c.scanned() && !c.verified()) bad++;
    }
    std::string s = fmt::format("{} clicks, {} with a window", n, withW);
    if (bad) s += fmt::format(", {} unverified", bad);
    s += fmt::format(" | {} | {:.0f} TPS", r.source, r.tps);
    if (r.replayCheck == RC_Mismatch) s += " | replay does NOT match the attempt";
    return s;
}

// ------------------------------------------------------------------------------------------------------------------
// active run
// ------------------------------------------------------------------------------------------------------------------
void recomputeSummary() {
    auto& a = app();
    Summary s;
    std::vector<double> ws;
    s.total = int(a.run.clicks.size());
    for (auto const& c : a.run.clicks) {
        if (c.shouldDraw) s.active++;
        if (c.flags & CF_Unreachable) s.unreachable++;
        if (c.scanned() && !c.verified()) s.unverified++;
        if (!c.shouldDraw || !c.hasWindow()) continue;
        s.withWindow++;
        if (c.measured()) s.measured++;
        if (c.flags & (CF_EarlyCapped | CF_LateCapped)) s.capped++;
        ws.push_back(c.window());
    }
    if (!ws.empty()) {
        std::sort(ws.begin(), ws.end());
        s.minWindow = ws.front();
        s.medianWindow = ws.size() % 2 ? ws[ws.size() / 2] : 0.5 * (ws[ws.size() / 2 - 1] + ws[ws.size() / 2]);
    }
    a.summary = s;
}

void setActiveRun(Run run, bool persist) {
    auto& a = app();
    a.run = std::move(run);
    a.hasRun = true;
    a.revision++;
    recomputeSummary();
    if (persist && a.run.levelID != 0) {
        std::string err;
        if (!saveRunFile(a.run, runPathFor(a.run.levelID), &err)) log::warn("AFWC: could not save run: {}", err);
    }
}

void touchRun(bool persist) {
    auto& a = app();
    a.revision++;
    recomputeSummary();
    if (persist && a.run.levelID != 0) saveRunFile(a.run, runPathFor(a.run.levelID), nullptr);
}

void clearLive() {
    auto& a = app();
    a.live.clear();
    a.liveCtx.clear();
    a.liveHeld[0] = a.liveHeld[1] = false;
    a.liveUnsupported = false;
    a.liveTick = 0;
}

void clearActiveRun() {
    auto& a = app();
    a.run = Run();
    a.hasRun = false;
    a.revision++;
    a.summary = Summary();
}

Run runFromLive(GJGameLevel* level, uint8_t outcome, int32_t endTick) {
    auto& a = app();
    Run r;
    r.levelID = level ? int(level->m_levelID.value()) : 0;
    r.levelName = level ? std::string(level->m_levelName) : std::string();
    r.tps = opt<double>("record-tps");
    r.endTick = endTick;
    r.outcome = outcome;
    r.completed = outcome == OUT_Completed;
    r.scannable = !a.liveUnsupported;
    r.source = "recorded";
    r.events = a.live;
    deriveClicks(r);
    for (auto& c : r.clicks) {
        auto it = a.liveCtx.find({c.tick, c.p2});
        if (it != a.liveCtx.end()) {
            c.x = it->second.x; c.y = it->second.y; c.percent = it->second.percent; c.mode = it->second.mode;
        }
    }
    return r;
}

// ------------------------------------------------------------------------------------------------------------------
// HUD labels
// ------------------------------------------------------------------------------------------------------------------
void markerLook(Click const& c, Color4& color, std::string& text) {
    auto& a = app();
    double w = c.window();
    color = {1, 1, 1, 1};
    text = c.hasWindow() ? formatWindow(w) : std::string("?");
    if (!c.hasWindow()) { color = {0.7f, 0.7f, 0.7f, 1}; return; }
    for (auto const& lp : a.labels) {
        if (lp.matches(w, c.ifCount)) { color = lp.color; break; }
    }
}

}  // namespace afwc
