#include "Overlay.hpp"
#include "../app/App.hpp"
#include "Shared.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace afwc::overlay {

namespace {

struct Marker {
    Ref<CCNode> node;
    CCPoint pos;      // object-layer coordinates
    float age = 0, life = 1.5f;
    float scale = 1.f;
};

struct Ov {
    PlayLayer* pl = nullptr;
    Ref<CCNode> hud, info;
    std::vector<Ref<CCLabelBMFont>> hudLabels;
    std::vector<int> hudLabelIdx;            // index into app().labels for every hud label
    Ref<CCLabelBMFont> infoLabel;
    std::vector<Marker> markers;
    std::vector<int> counts;                 // per label
    size_t cursor = 0;                       // next click that has not been triggered
    int lastClick = -1;
    int32_t lastTick = -1;
    uint32_t rev = ~0u;
    bool dirtyText = true;
};

Ov& ov() {
    static Ov o;
    return o;
}

bool on() { return opt<bool>("enabled"); }

ccColor3B toC3(Color4 const& c) {
    auto q = [](float f) { return GLubyte(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
    return {q(c.r), q(c.g), q(c.b)};
}

void clearMarkers() {
    for (auto& m : ov().markers) if (m.node) m.node->removeFromParent();
    ov().markers.clear();
}

void destroyNodes() {
    auto& o = ov();
    clearMarkers();
    for (auto* n : {&o.hud, &o.info}) {
        if (*n) (*n)->removeFromParent();
        *n = nullptr;
    }
    o.hudLabels.clear();
    o.hudLabelIdx.clear();
    o.infoLabel = nullptr;
}

void buildNodes() {
    auto& o = ov();
    if (!o.pl || !o.pl->m_uiLayer) return;
    destroyNodes();
    auto& a = app();
    auto win = CCDirector::get()->getWinSize();
    auto* ui = o.pl->m_uiLayer;
    float hs = float(opt<double>("hud-scale"));

    // label counters, top-left
    o.hud = CCNode::create();
    o.hud->setID("hud"_spr);
    o.hud->setPosition({6.f, win.height - 6.f});
    o.hud->setZOrder(900);
    float y = 0;
    for (size_t i = 0; i < a.labels.size(); i++) {
        auto const& lp = a.labels[i];
        if (!lp.showInHud) continue;
        auto* l = CCLabelBMFont::create("", "bigFont.fnt");
        l->setAnchorPoint({0.f, 1.f});
        l->setScale(hs);
        l->setPosition({0.f, y});
        l->setColor(toC3(lp.color));
        o.hud->addChild(l);
        o.hudLabels.push_back(l);
        o.hudLabelIdx.push_back(int(i));
        y -= 40.f * hs;
    }
    o.hud->setVisible(opt<bool>("show-hud"));
    ui->addChild(o.hud);

    // info line, bottom centre
    o.info = CCNode::create();
    o.info->setID("info"_spr);
    o.info->setZOrder(900);
    o.infoLabel = CCLabelBMFont::create("", "bigFont.fnt");
    o.infoLabel->setScale(0.3f);
    o.info->addChild(o.infoLabel);
    o.info->setPosition({win.width / 2, 10.f});
    ui->addChild(o.info);

    o.dirtyText = true;
}

void recount(int32_t tick) {
    auto& o = ov();
    auto& a = app();
    o.counts.assign(a.labels.size(), 0);
    o.cursor = 0;
    o.lastClick = -1;
    auto& cl = a.run.clicks;
    if (a.hasRun) {
        while (o.cursor < cl.size() && cl[o.cursor].tick < tick) {
            auto const& c = cl[o.cursor];
            if (c.shouldDraw && c.hasWindow())
                for (size_t i = 0; i < a.labels.size(); i++)
                    if (a.labels[i].matches(c.window(), c.ifCount)) o.counts[i]++;
            o.lastClick = int(o.cursor);
            o.cursor++;
        }
    }
    o.dirtyText = true;
}

void spawnMarker(Click const& c, int idx) {
    auto& o = ov();
    if (!o.pl || !o.pl->m_uiLayer || !o.pl->m_objectLayer) return;
    PlayerObject* p = c.p2 && o.pl->m_player2 ? o.pl->m_player2 : o.pl->m_player1;
    if (!p) return;
    Color4 col;
    std::string text;
    markerLook(c, col, text);
    auto* node = CCNode::create();
    node->setID("marker"_spr);
    node->setZOrder(800);
    auto* dot = CCDrawNode::create();
    ccColor4F outer{col.r, col.g, col.b, 0.9f};
    ccColor4F inner{0.f, 0.f, 0.f, 0.55f};
    dot->drawDot({0, 0}, 15.f, outer);
    dot->drawDot({0, 0}, 12.f, inner);
    node->addChild(dot);
    auto* l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    l->setScale(text.size() > 4 ? 0.3f : 0.4f);
    l->setColor(toC3(col));
    node->addChild(l);
    if (opt<bool>("marker-detail") && c.measured()) {
        auto* d = CCLabelBMFont::create(fmt::format("-{}/+{}", c.early, c.late).c_str(), "chatFont.fnt");
        d->setScale(0.5f);
        d->setColor(toC3(col));
        d->setPosition({0.f, -22.f});
        node->addChild(d);
    }
    Marker m;
    m.node = node;
    m.pos = p->getPosition() + CCPoint{0.f, 22.f};
    m.life = float(opt<double>("marker-seconds"));
    m.scale = float(opt<double>("marker-scale"));
    o.pl->m_uiLayer->addChild(node);
    o.markers.push_back(std::move(m));
    (void)idx;
    while (o.markers.size() > 24) {
        if (o.markers.front().node) o.markers.front().node->removeFromParent();
        o.markers.erase(o.markers.begin());
    }
}

void trigger(size_t idx, bool effects) {
    auto& o = ov();
    auto& a = app();
    auto const& c = a.run.clicks[idx];
    o.lastClick = int(idx);
    if (!c.shouldDraw || !c.hasWindow()) return;
    for (size_t i = 0; i < a.labels.size(); i++) {
        if (a.labels[i].matches(c.window(), c.ifCount)) o.counts[i]++;
    }
    o.dirtyText = true;
    if (!effects) return;
    if (opt<bool>("show-markers")) spawnMarker(c, int(idx));
}

void updateTexts() {
    auto& o = ov();
    auto& a = app();
    o.dirtyText = false;
    for (size_t i = 0; i < o.hudLabels.size(); i++) {
        int li = o.hudLabelIdx[i];
        if (li < 0 || li >= int(a.labels.size())) continue;
        int n = li < int(o.counts.size()) ? o.counts[li] : 0;
        o.hudLabels[i]->setString(fmt::format("{}: {}", a.labels[li].text, n).c_str());
    }
    if (o.infoLabel) {
        std::string s;
        if (opt<bool>("show-info-line") && a.hasRun && o.lastClick >= 0 && o.lastClick < int(a.run.clicks.size())) {
            auto const& c = a.run.clicks[o.lastClick];
            s = fmt::format("click {}/{}", o.lastClick + 1, a.run.clicks.size());
            if (c.hasWindow()) {
                s += fmt::format("  window {}", formatWindow(c.window()));
                if (c.measured()) s += fmt::format(" (-{}/+{}{})", c.early, c.late, (c.flags & (CF_EarlyCapped | CF_LateCapped)) ? "+" : "");
                s += fmt::format("  needs >= {:.0f} Hz", a.run.tps / c.window());
            } else if (c.flags & CF_Unreachable) s += "  (after the end of the replay)";
            else if (c.scanned()) s += "  (not verified)";
            else s += "  (not scanned)";
            if (a.run.replayCheck == RC_Mismatch) s += "  [replay mismatch]";
        }
        o.infoLabel->setString(s.c_str());
    }
}

}  // namespace

void attach(PlayLayer* pl) {
    auto& o = ov();
    o = Ov{};
    o.pl = pl;
    buildNodes();
    recount(0);
    o.rev = app().revision;
}

void detach() {
    auto& o = ov();
    destroyNodes();
    o = Ov{};
}

void refresh() {
    auto& o = ov();
    if (!o.pl) return;
    o.rev = ~0u;
}

void rewound(PlayLayer* pl, int32_t tick) {
    auto& o = ov();
    if (!o.pl || o.pl != pl) return;
    clearMarkers();
    recount(tick);
    o.lastTick = tick;
}

void frame(PlayLayer* pl, int32_t tick, float dt) {
    auto& o = ov();
    if (!o.pl || o.pl != pl) return;
    if (!on()) {
        if (o.hud) o.hud->setVisible(false);
        if (o.info) o.info->setVisible(false);
        clearMarkers();
        return;
    }
    if (o.rev != app().revision) {
        o.rev = app().revision;
        buildNodes();
        recount(tick);
        o.lastTick = tick;
    }
    if (o.hud) o.hud->setVisible(opt<bool>("show-hud"));
    if (o.info) o.info->setVisible(true);
    if (tick < o.lastTick) {
        clearMarkers();
        recount(tick);
    }
    o.lastTick = tick;

    auto& a = app();
    if (a.hasRun) {
        auto& cl = a.run.clicks;
        // everything that already lies in the past of this frame; only the last few get effects, so a jump never floods the screen
        size_t end = o.cursor;
        while (end < cl.size() && cl[end].tick < tick) end++;
        size_t effectsFrom = end > 3 ? end - 3 : 0;
        for (; o.cursor < end; o.cursor++) trigger(o.cursor, o.cursor >= effectsFrom);
    }
    if (o.dirtyText) updateTexts();

    // markers follow the world, fade and expire
    if (pl->m_objectLayer) {
        float layerScale = std::abs(pl->m_objectLayer->getScaleY());
        for (auto it = o.markers.begin(); it != o.markers.end();) {
            it->age += dt;
            if (it->age >= it->life || !it->node) {
                if (it->node) it->node->removeFromParent();
                it = o.markers.erase(it);
                continue;
            }
            it->node->setPosition(pl->m_objectLayer->convertToWorldSpace(it->pos));
            it->node->setScale(it->scale * std::max(0.4f, layerScale));
            float left = it->life - it->age;
            GLubyte op = left < 0.4f ? GLubyte(255.f * left / 0.4f) : 255;
            if (auto* ch = it->node->getChildren()) {
                for (auto* n : CCArrayExt<CCNode*>(ch)) {
                    if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(n)) rgba->setOpacity(op);
                }
            }
            ++it;
        }
    }
}

}  // namespace afwc::overlay
