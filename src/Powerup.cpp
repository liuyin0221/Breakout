#include "Powerup.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string LoadTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::string();
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static bool ExtractStringValue(const std::string& source, const std::string& key, std::string& outValue) {
    size_t pos = source.find(key);
    if (pos == std::string::npos) return false;
    pos = source.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = source.find('"', pos);
    if (pos == std::string::npos) return false;
    size_t end = source.find('"', pos + 1);
    if (end == std::string::npos) return false;
    outValue = source.substr(pos + 1, end - pos - 1);
    return true;
}

static bool ExtractFloatValue(const std::string& source, const std::string& key, float& outValue) {
    size_t pos = source.find(key);
    if (pos == std::string::npos) return false;
    pos = source.find(':', pos);
    if (pos == std::string::npos) return false;
    size_t start = source.find_first_of("-0123456789", pos);
    if (start == std::string::npos) return false;
    size_t end = start;
    while (end < source.size() && (std::isdigit(source[end]) || source[end] == '.' || source[end] == '-' || source[end] == '+')) end++;
    outValue = std::stof(source.substr(start, end - start));
    return true;
}

static bool ExtractColorValue(const std::string& source, const std::string& key, Color& outColor) {
    size_t pos = source.find(key);
    if (pos == std::string::npos) return false;
    pos = source.find('[', pos);
    if (pos == std::string::npos) return false;
    size_t end = source.find(']', pos);
    if (end == std::string::npos) return false;
    std::string data = source.substr(pos + 1, end - pos - 1);
    std::istringstream stream(data);
    int values[4] = {255, 255, 255, 255};
    char comma;
    for (int i = 0; i < 4; i++) {
        if (!(stream >> values[i])) return false;
        if (i < 3) {
            if (!(stream >> comma)) return false;
        }
    }
    outColor = Color{(unsigned char)values[0], (unsigned char)values[1], (unsigned char)values[2], (unsigned char)values[3]};
    return true;
}

Powerup::Powerup(PowerupType type, const std::string& name, float x, float y, float size, Color color)
    : type(type), color(color), alive(true), fallSpeed(160.0f), name(name) {
    rect = { x - size / 2.0f, y - size / 2.0f, size, size };
}

void Powerup::Update(float delta) {
    rect.y += fallSpeed * delta;
    if (rect.y > GetScreenHeight() + 40) alive = false;
}

void Powerup::Draw(const Font& font) const {
    Vector2 center = { rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f };
    DrawCircleGradient((int)center.x, (int)center.y, rect.width * 0.6f, Fade(color, 0.15f), Fade(WHITE, 0.05f));
    DrawRectangleRounded(rect, 0.25f, 8, Fade(color, 0.8f));
    DrawRectangleRoundedLines(rect, 0.25f, 8, WHITE);
    DrawTextEx(font, name.c_str(), {rect.x + 6, rect.y + 8}, 16, 4, WHITE);
}

bool Powerup::CheckCatch(const Rectangle& paddle) {
    if (!alive) return false;
    if (CheckCollisionRecs(rect, paddle)) {
        alive = false;
        return true;
    }
    return false;
}

PowerupType PowerupFactory::ParseType(const std::string& type) {
    if (type == "extend_paddle") return PowerupType::ExtendPaddle;
    if (type == "multi_ball") return PowerupType::MultiBall;
    if (type == "slow_ball") return PowerupType::SlowBall;
    return PowerupType::Unknown;
}

Powerup PowerupFactory::Create(const PowerupConfig& cfg, float x, float y) {
    return Powerup(cfg.type, cfg.name, x, y, cfg.size, cfg.color);
}

const PowerupConfig* PowerupFactory::FindConfig(PowerupType type, const std::vector<PowerupConfig>& configs) {
    for (const auto& cfg : configs) {
        if (cfg.type == type) return &cfg;
    }
    return nullptr;
}

bool PowerupFactory::LoadConfig(const std::string& path, std::vector<PowerupConfig>& configs) {
    std::string source = LoadTextFile(path);
    if (source.empty()) return false;

    size_t index = 0;
    while (true) {
        size_t objectStart = source.find('{', index);
        if (objectStart == std::string::npos) break;
        size_t objectEnd = source.find('}', objectStart);
        if (objectEnd == std::string::npos) break;
        std::string objectText = source.substr(objectStart, objectEnd - objectStart + 1);

        std::string typeText;
        std::string nameText;
        float duration = 0.0f;
        float chance = 0.0f;
        float size = 24.0f;
        Color color = WHITE;

        if (!ExtractStringValue(objectText, "\"type\"", typeText) ||
            !ExtractStringValue(objectText, "\"name\"", nameText) ||
            !ExtractFloatValue(objectText, "\"duration\"", duration) ||
            !ExtractFloatValue(objectText, "\"chance\"", chance) ||
            !ExtractFloatValue(objectText, "\"size\"", size) ||
            !ExtractColorValue(objectText, "\"color\"", color)) {
            index = objectEnd + 1;
            continue;
        }

        PowerupConfig cfg;
        cfg.type = ParseType(typeText);
        cfg.name = nameText;
        cfg.duration = duration;
        cfg.chance = chance;
        cfg.color = color;
        cfg.size = size;
        configs.push_back(cfg);
        index = objectEnd + 1;
    }

    return !configs.empty();
}
