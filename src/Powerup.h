#ifndef POWERUP_H
#define POWERUP_H

#include <string>
#include <vector>
#include "raylib.h"

enum class PowerupType {
    ExtendPaddle,
    MultiBall,
    SlowBall,
    Unknown,
};

struct PowerupConfig {
    PowerupType type;
    std::string name;
    float duration;
    float chance;
    Color color;
    float size;
};

struct ActivePowerup {
    PowerupType type;
    float remaining;
    bool active;
};

class Powerup {
private:
    Rectangle rect;
    PowerupType type;
    Color color;
    bool alive;
    float fallSpeed;
    std::string name;

public:
    Powerup(PowerupType type, const std::string& name, float x, float y, float size, Color color);
    void Update(float delta);
    void Draw(const Font& font) const;
    bool CheckCatch(const Rectangle& paddle);
    Rectangle GetRect() const { return rect; }
    bool IsAlive() const { return alive; }
    PowerupType GetType() const { return type; }
    const std::string& GetName() const { return name; }
};

class PowerupFactory {
public:
    static bool LoadConfig(const std::string& path, std::vector<PowerupConfig>& configs);
    static Powerup Create(const PowerupConfig& cfg, float x, float y);
    static PowerupType ParseType(const std::string& name);
    static const PowerupConfig* FindConfig(PowerupType type, const std::vector<PowerupConfig>& configs);
};

#endif
