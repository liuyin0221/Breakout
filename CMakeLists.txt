cmake_minimum_required(VERSION 3.10)
project(Breakout)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR}/raylib-5.0/src)
link_directories(${CMAKE_SOURCE_DIR}/raylib-5.0/build/raylib)

add_executable(breakout_week2 
    src/main.cpp 
    src/Ball.cpp 
    src/Paddle.cpp 
    src/Brick.cpp
    src/Powerup.cpp
    src/Particle.cpp
)

target_link_libraries(breakout_week2 raylib m pthread dl rt X11)
