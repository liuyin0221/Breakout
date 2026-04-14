#ifndef BRICK_H
#define BRICK_H
#include "raylib.h"

class Brick {
private:
    Rectangle rect;
    bool active;
    Color color;

public:
    Brick(float x, float y, float width, float height, Color c);
    void Draw();
    bool IsActive() { return active; }
    void SetActive(bool a) { active = a; }
    Rectangle GetRect() { return rect; }
};

#endif
