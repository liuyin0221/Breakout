#include "Brick.h"

// 补全完整的构造函数实现
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

// 新增：设置砖块颜色的函数实现
void Brick::SetColor(Color c) {
    color = c;
}