// AFWC - HUD counter labels: which window sizes are counted together, and their colour (pure C++).
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace afwc {

struct Color4 {
    float r = 1, g = 1, b = 1, a = 1;
    bool operator==(Color4 const&) const = default;
};

// "5", "0.5" and fractions such as "1/2" are accepted. Empty/invalid text yields `def`.
inline double parseWindowExpr(std::string const& text, double def) {
    if (text.empty()) return def;
    auto parseNum = [](std::string const& s, double& out) {
        try {
            size_t used = 0;
            out = std::stod(s, &used);
            return used == s.size() || s.find_first_not_of(" \t", used) == std::string::npos;
        } catch (...) { return false; }
    };
    auto slash = text.find('/');
    if (slash != std::string::npos) {
        if (text.find('/', slash + 1) != std::string::npos) return def;
        double n, d;
        if (!parseNum(text.substr(0, slash), n) || !parseNum(text.substr(slash + 1), d)) return def;
        if (std::abs(d) < 1e-9) return def;
        return n / d;
    }
    double v;
    return parseNum(text, v) ? v : def;
}

struct LabelPreset {
    int id = 0;
    bool useIF = false;                  // count by inputs-per-frame instead of by window
    std::string minW, maxW, minIF, maxIF;
    std::string text = "Label";
    Color4 color;
    bool showInHud = true;
    double minVal = 0, maxVal = 999999;

    void updateBounds() {
        if (useIF) { minVal = parseWindowExpr(minIF, 1.0); maxVal = parseWindowExpr(maxIF, 999999.0); }
        else { minVal = parseWindowExpr(minW, 0.0); maxVal = parseWindowExpr(maxW, 999999.0); }
    }
    bool matches(double window, int ifCount) const {
        double v = useIF ? double(ifCount) : window;
        return v >= minVal - 1e-9 && v <= maxVal + 1e-9;
    }
};

inline std::string formatWindow(double v) {
    if (std::abs(v) < 1e-9) return "0";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

// Default labels: one per window class, red = hardest.
inline std::vector<LabelPreset> defaultLabelPresets() {
    auto mk = [](int id, const char* text, const char* mn, const char* mx, Color4 c) {
        LabelPreset p;
        p.id = id; p.text = text; p.minW = mn; p.maxW = mx; p.color = c; p.showInHud = true;
        p.updateBounds();
        return p;
    };
    return {
        mk(0, "1F", "1", "1", {1.0f, 0.15f, 0.15f, 1}),
        mk(1, "2F", "2", "2", {1.0f, 0.5f, 0.0f, 1}),
        mk(2, "3F", "3", "3", {1.0f, 0.92f, 0.1f, 1}),
        mk(3, "4-6F", "4", "6", {0.2f, 1.0f, 0.3f, 1}),
        mk(4, "7F+", "7", "999999", {0.2f, 0.75f, 1.0f, 1}),
    };
}

}  // namespace afwc
