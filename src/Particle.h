#ifndef PARTICLE_H
#define PARTICLE_H

#include <vector>
#include "raylib.h"

struct Particle {
    Vector2 position;
    Vector2 velocity;
    Color color;
    float size;
    float life;
};

class ParticleSystem {
private:
    std::vector<Particle> particles;

public:
    ParticleSystem();
    void EmitExplosion(const Vector2& center, Color color, int count);
    void EmitAura(const Rectangle& rect, Color color);
    void Update(float delta);
    void Draw() const;
};

#endif
