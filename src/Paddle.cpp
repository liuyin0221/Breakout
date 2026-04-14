#include "Paddle.h"

Paddle::Paddle(float x, float y, float width, float height) {
    rect = { x, y, width, height };
    screenWidth = 800;
}

void Paddle::MoveLeft(float speed) {
    rect.x -= speed;
    if (rect.x < 5) rect.x = 5;
}

void Paddle::MoveRight(float speed) {
    rect.x += speed;
    if (rect.x + rect.width > screenWidth - 5) rect.x = screenWidth - rect.width - 5;
}

void Paddle::SetWidth(float width) {
    rect.width = width;
    if (rect.x + rect.width > screenWidth - 5) rect.x = screenWidth - rect.width - 5;
}

void Paddle::Draw() {
    DrawRectangleRec(rect, BLUE);
    DrawRectangleLinesEx(rect, 2, SKYBLUE);
}
