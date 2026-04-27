#ifndef BRICK_H
#define BRICK_H
#include "raylib.h"

class Brick {
private:
    Rectangle rect;
    bool active;
    Color color;
public:
    // 补全完整的构造函数声明
    Brick(float x, float y, float width, float height, Color c);
    void Draw();
    bool IsActive() { return active; }
    void SetActive(bool a) { active = a; }
    Rectangle GetRect() { return rect; }
    // 新增：设置砖块颜色的函数声明
    void SetColor(Color c);
};

#endif
