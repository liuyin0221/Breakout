# Breakout 联机打砖块游戏

## 项目简介
本项目基于 Raylib 图形库与 C++ 开发，实现了经典打砖块游戏玩法，
集成双人联机对战、道具系统、多关卡 JSON 加载、游戏存档、排行榜、异步加载、网格碰撞优化等功能。

## 开发环境
- 编程语言：C++17
- 编译工具：CMake
- 依赖库：Raylib、ENet、nlohmann-json
- 运行环境：WSL / Linux

## 项目结构
Breakout/
├── src/           源代码与头文件
├── build/         编译输出目录
├── fonts/         字体资源
├── docs/          文档
├── CMakeLists.txt 构建配置
└── README.md      项目说明

## 编译与运行
### 编译
cd build
cmake ..
make

### 运行
主机模式：
./breakout_week2 host

客户端模式：
./breakout_week2 client

## 操作说明
方向键 ← →  移动挡板
空格键        发射小球
P             暂停/继续
R             重新开始
M             查看排行榜
L             异步加载演示
N             切换关卡

## 功能实现
1. 基础打砖块物理碰撞与反弹逻辑
2. 联机对战（主机 + 客户端）
3. 多道具系统：加长板、多球、减速球
4. JSON 关卡配置读取
5. 游戏存档与读档
6. 本地分数排行榜
7. 异步资源加载
8. 网格空间优化碰撞检测
9. 粒子特效系统