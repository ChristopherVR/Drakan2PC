#include "world/Json.h"

#include <cmath>
#include <cstdio>

namespace world {
namespace {

void appendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(c));
                    out += b;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

std::string num(float v) {
    if (!std::isfinite(v))
        return "null";
    char b[40];
    std::snprintf(b, sizeof b, "%.3f", static_cast<double>(v));
    return b;
}

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "\"0x%x\"", v);
    return b;
}

} // namespace

std::string toJson(const Snapshot& s) {
    std::string o;
    o.reserve(128 + s.entities.size() * 220);
    o += "{\n  \"status\": ";
    appendEscaped(o, readStatusName(s.status));
    o += ",\n  \"sequence\": " + std::to_string(s.sequence);
    o += ",\n  \"levelName\": ";
    appendEscaped(o, s.levelName);
    o += ",\n  \"levelPath\": ";
    appendEscaped(o, s.levelPath);
    o += ",\n  \"player\": " + hex(s.playerHandle);
    o += ",\n  \"camera\": " + hex(s.cameraHandle);
    o += ",\n  \"registryCapacity\": " + std::to_string(s.registryCapacity);
    o += ",\n  \"registryHighWater\": " + std::to_string(s.registryHighWater);
    o += ",\n  \"staleSlots\": " + std::to_string(s.staleSlots);
    o += ",\n  \"entities\": [";
    bool first = true;
    for (const EntitySnapshot& e : s.entities) {
        o += first ? "\n    " : ",\n    ";
        first = false;
        o += "{\"handle\": " + hex(e.handle) + ", \"ref\": " + std::to_string(e.ref) +
             ", \"class\": " + hex(e.classId) + ", \"type\": " + std::to_string(e.type) +
             ", \"resIndex\": " + std::to_string(e.resourceIndex) + ", \"resId\": " + hex(e.resourceId) +
             ", \"model\": " + hex(e.modelId) + ", \"flags\": " + hex(e.flags) +
             ", \"lists\": " + std::to_string(e.listMask) + ", \"pos\": [" + num(e.position.x) + ", " +
             num(e.position.y) + ", " + num(e.position.z) + "], \"yaw\": " + num(e.yaw);
        if (e.hasHp())
            o += ", \"hp\": " + std::to_string(e.hp) + ", \"hpMax\": " + std::to_string(e.hpMax) + ", \"hpKind\": \"" +
                 (e.hpKind == HpKind::PlayerHp ? "player" : "durability") + "\"";
        o += "}";
    }
    o += first ? "]\n}\n" : "\n  ]\n}\n";
    return o;
}

} // namespace world
