#ifndef BRICK_H
#define BRICK_H

#include "raylib.h"

class Brick {
private:
    Rectangle rect;
    Color color;
    bool active;

public:
    // 构造函数
    Brick() = default;
    // 把类内的实现删掉，只留声明
Brick(float x, float y, float w, float h, Color c);
bool IsActive() const;
Rectangle GetRect() const;
Color GetColor() const;
void SetActive(bool a);
void SetColor(Color newColor);
void Draw();
};

#endif