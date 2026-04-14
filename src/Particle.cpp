#include "Particle.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

ParticleSystem::ParticleSystem() {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(nullptr));
        seeded = true;
    }
}

void ParticleSystem::EmitExplosion(const Vector2& center, Color color, int count) {
    for (int i = 0; i < count; i++) {
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        float speed = (float)(rand() % 120 + 80) / 60.0f;
        Particle p;
        p.position = center;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.color = color;
        p.size = (float)(rand() % 4 + 2);
        p.life = (float)(rand() % 40 + 40) / 60.0f;
        particles.push_back(p);
    }
}

void ParticleSystem::EmitAura(const Rectangle& rect, Color color) {
    Vector2 center = { rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f };
    for (int i = 0; i < 10; i++) {
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        Particle p;
        p.position = center;
        p.velocity = { std::cos(angle) * 10.0f, std::sin(angle) * 10.0f };
        p.color = Fade(color, 0.65f);
        p.size = (float)(rand() % 3 + 3);
        p.life = 0.4f + (rand() % 30) / 100.0f;
        particles.push_back(p);
    }
}

void ParticleSystem::Update(float delta) {
    for (auto& p : particles) {
        p.position.x += p.velocity.x;
        p.position.y += p.velocity.y;
        p.life -= delta;
        p.velocity.y += 20.0f * delta;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), particles.end());
}

void ParticleSystem::Draw() const {
    for (const auto& p : particles) {
        float progress = p.life * 2.5f;
        Color color = Fade(p.color, progress);
        DrawCircleV(p.position, p.size, color);
    }
}
