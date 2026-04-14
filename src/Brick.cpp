#include "Brick.h"

Brick::Brick(float x, float y, float width, float height, Color c) {
    rect = { x, y, width, height };
    active = true;
    color = c;
}

void Brick::Draw() {
    if (active) {
        DrawRectangleRec(rect, color);
        DrawRectangleLinesEx(rect, 1, WHITE);
    }
}
