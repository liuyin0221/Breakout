#include "Brick.h"

Brick::Brick(float x, float y, float width, float height, Color c) {
    rect = {x, y, width, height};
    color = c;
    active = true;
}

bool Brick::IsActive() const {
    return active;
}

Rectangle Brick::GetRect() const {
    return rect;
}

Color Brick::GetColor() const {
    return color;
}

void Brick::SetActive(bool a) {
    active = a;
}

void Brick::SetColor(Color c) {
    color = c;
}

void Brick::Draw() {
    if (active) {
        DrawRectangleRec(rect, color);
        DrawRectangleLinesEx(rect, 1, WHITE);
    }
}