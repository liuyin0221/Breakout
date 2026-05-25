cmake_minimum_required(VERSION 3.10)
project(Breakout)

set(CMAKE_CXX_STANDARD 17)

# 你原来的 raylib 配置
include_directories(${CMAKE_SOURCE_DIR}/raylib-5.0/src)
link_directories(${CMAKE_SOURCE_DIR}/raylib-5.0/build/raylib)

# 1. 关键：加上这一行，让编译器找到系统里的 nlohmann-json 头文件
include_directories(/usr/include)
# ==================== ENet 新增部分（我帮你加好了）====================
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENET REQUIRED libenet)
include_directories(${ENET_INCLUDE_DIRS})
# ====================================================================

add_executable(breakout_week2 
    src/main.cpp 
    src/Ball.cpp 
    src/Paddle.cpp 
    src/Brick.cpp
    src/Powerup.cpp
    src/Particle.cpp
)

# 2. 关键：只保留需要链接的库，去掉 nlohmann_json
target_link_libraries(breakout_week2 raylib m pthread dl rt X11 enet)
# ==================== ENet 链接（我帮你加好了）====================
target_link_libraries(breakout_week2 raylib m pthread dl rt X11 ${ENET_LIBRARIES})
# =================================================================