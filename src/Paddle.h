#ifndef PADDLE_H
#define PADDLE_H
#include "raylib.h"

class Paddle {
private:
    Rectangle rect;
    float screenWidth;

public:
    Paddle(float x, float y, float width, float height);
    void MoveLeft(float speed);
    void MoveRight(float speed);
    void Draw();
    void SetWidth(float width);
    float GetWidth() const { return rect.width; }
    Rectangle GetRect() const { return rect; }
};

#endif
