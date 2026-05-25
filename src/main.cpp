#include "raylib.h"
#include "Ball.h"
#include "Paddle.h"
#include "Brick.h"
#include "Powerup.h"
#include "Particle.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <enet/enet.h>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <future>
#include <chrono>
#include <unordered_map>
#include <fstream>

// JSON解析库头文件引入
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ===================== 联机核心结构体 =====================
#pragma pack(push, 1)
// 全局游戏状态同步结构体
struct GameState {
    float ballX, ballY;
    float ballSpeedX, ballSpeedY;
    float paddle1X;
    float paddle2X;
    int score;
    int lives;
    double timestamp;
};
// 玩家挡板输入数据结构
struct PaddleInput {
    float x;
};
#pragma pack(pop)

// ===================== 全局网络变量 =====================
ENetHost* netHost = nullptr;
ENetPeer* netPeer = nullptr;
bool isHost = false;
GameState remoteState = {};
GameState lastState = {};
bool hasLastState = false;
PaddleInput currentInput = {};

// ===================== 多线程异步加载相关 =====================
enum class LoadState { IDLE, LOADING, DONE };
LoadState loadState = LoadState::IDLE;
std::future<void> loadFuture;
std::mutex loadMutex;
bool bricksLoaded = false;

// 线程安全的纹理缓存
class TextureCache {
private:
    std::unordered_map<std::string, Texture2D> cache;
    mutable std::mutex mtx;

    TextureCache() = default;
public:
// 获取单例对象
        static TextureCache& getInstance() {
        static TextureCache instance;
        return instance;
    }
// 读取缓存纹理
    Texture2D get(const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(path);
        if (it != cache.end()) return it->second;
        
        Texture2D tex = LoadTexture(path.c_str());
        cache[path] = tex;
        return tex;
    }
// 清空缓存释放资源
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& pair : cache) {
            UnloadTexture(pair.second);
        }
        cache.clear();
    }
};

// 模拟异步加载函数
void LoadLevelAsync() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::lock_guard<std::mutex> lock(loadMutex);
    bricksLoaded = true;
}

// ===================== 网格空间划分（优化碰撞检测） =====================
#define GRID_COLS 16
#define GRID_ROWS 12
#define CELL_WIDTH (800.0f / GRID_COLS)
#define CELL_HEIGHT (600.0f / GRID_ROWS)
std::vector<Brick*> grid[GRID_ROWS][GRID_COLS];

void UpdateBrickGrid(std::vector<Brick>& bricks) {
    for (int i = 0; i < GRID_ROWS; i++) {
        for (int j = 0; j < GRID_COLS; j++) {
            grid[i][j].clear();
        }
    }
    for (auto& brick : bricks) {
        if (!brick.IsActive()) continue;
        int col = (int)(brick.GetRect().x / CELL_WIDTH);
        int row = (int)(brick.GetRect().y / CELL_HEIGHT);
        if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS) {
            grid[row][col].push_back(&brick);
        }
    }
}

// ===================== JSON关卡加载 =====================
// 砖块配色映射数组
std::vector<Color> colorMap = {RED, ORANGE, YELLOW, GREEN, BLUE};
// 从JSON文件加载关卡砖块数据
void LoadLevelFromJSON(int level, std::vector<Brick>& bricks, int& winCount) {
    bricks.clear();
    std::string filename = "levels/level" + std::to_string(level) + ".json";
    std::ifstream file(filename);
// 文件不存在使用默认关卡
    if (!file.is_open()) {
        TraceLog(LOG_WARNING, "关卡文件不存在，使用默认配置");
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 8; col++) {
                bricks.emplace_back(50 + col * 95, 80 + row * 35, 85, 25, colorMap[row]);
            }
        }
        winCount = bricks.size();
        return;
    }
// 解析JSON关卡配置
    try {
        json config;
        file >> config;
        int rows = config["bricks"]["rows"];
        int cols = config["bricks"]["cols"];
        int brickWidth = config["bricks"]["width"];
        int brickHeight = config["bricks"]["height"];
        auto layout = config["bricks"]["layout"];

        winCount = 0;
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                int type = layout[i][j];
                if (type != 0) {
                    Color color = colorMap[type - 1];
                    
float brickSpacing = 10.0f;
float startX = (800.0f - (cols * brickWidth + (cols - 1) * brickSpacing)) / 2; // 用800代替screenWidth
float startY = 80.0f;

bricks.emplace_back(
    startX + j * (brickWidth + brickSpacing),
    startY + i * (brickHeight + 5),
    brickWidth,
    brickHeight,
    color
);
                    winCount++;
                }
            }
        }
    } catch (const json::parse_error& e) {
        // JSON解析异常降级默认关卡
        TraceLog(LOG_ERROR, "JSON解析失败: %s", e.what());
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 8; col++) {
                bricks.emplace_back(50 + col * 95, 80 + row * 35, 85, 25, colorMap[row]);
            }
        }
        winCount = bricks.size();
    }
}

// ===================== 存档/读档 =====================
// 存档数据结构体
struct GameSave {
    int version = 1;
    int currentLevel = 1;
    int score = 0;
    int lives = 3;
};
// 保存游戏进度到本地文件
void SaveGame(const GameSave& save) {
    json j;
    j["version"] = save.version;
    j["current_level"] = save.currentLevel;
    j["score"] = save.score;
    j["lives"] = save.lives;

    std::ofstream file("save.json");
    if (file.is_open()) {
        file << j.dump(4);
        TraceLog(LOG_INFO, "游戏已存档");
    }
}
// 读取本地存档恢复游戏进度
bool LoadGame(GameSave& save) {
    std::ifstream file("save.json");
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;
        if (j["version"] == 1) {
            save.currentLevel = j["current_level"];
            save.score = j["score"];
            save.lives = j["lives"];
            return true;
        } else {
            TraceLog(LOG_WARNING, "存档版本不兼容，使用默认值");
            return false;
        }
    } catch (const json::parse_error& e) {
        TraceLog(LOG_ERROR, "存档解析失败: %s", e.what());
        return false;
    }
}

// ===================== 排行榜 =====================
// 单条分数记录结构
struct ScoreEntry {
    char name[32];
    int score;
    time_t timestamp;
};
// 排行榜管理类
class Leaderboard {
private:
    static const int MAX_ENTRIES = 10;
    ScoreEntry entries[MAX_ENTRIES];
    int count;
    const char* filename;
    
public:
// 构造初始化并加载榜单
    Leaderboard(const char* file) : count(0), filename(file) { Load(); }
    // 读取本地榜单文件
    void Load() {
        FILE* f = fopen(filename, "r");
        if (f) {
            count = 0;
            while (count < MAX_ENTRIES && fscanf(f, "%31s %d %ld", entries[count].name, &entries[count].score, &entries[count].timestamp) == 3) {
                count++;
            }
            fclose(f);
        }
    }
    // 保存榜单数据到文件
    void Save() {
        FILE* f = fopen(filename, "w");
        if (f) {
            for (int i = 0; i < count; i++) fprintf(f, "%s %d %ld\n", entries[i].name, entries[i].score, entries[i].timestamp);
            fclose(f);
        }
    }
    // 添加新分数并排序
    int AddScore(const char* name, int score) {
        int rank = 1;
        for (int i = 0; i < count; i++) {
            if (entries[i].score > score) rank++;
        }
        if (count >= MAX_ENTRIES && score <= entries[count - 1].score) return 0;
        ScoreEntry newEntry;
        strncpy(newEntry.name, name, 31); newEntry.name[31] = '\0';
        newEntry.score = score; newEntry.timestamp = time(nullptr);
        int pos = 0;
        while (pos < count && entries[pos].score >= score) pos++;
        if (count < MAX_ENTRIES) count++;
        for (int i = count - 1; i > pos; i--) entries[i] = entries[i - 1];
        entries[pos] = newEntry;
        Save();
        return rank;
    }
    // 根据排名获取记录
    bool GetEntry(int rank, ScoreEntry& entry) { 
        if (rank > 0 && rank <= count) { 
            entry = entries[rank - 1]; 
            return true; 
        } 
        return false; 
    }
// 获取榜单有效条数
    int GetCount() { return count; }
// 判断分数能否上榜
    bool CanEnter(int score) { 
        return count < MAX_ENTRIES || score > entries[count - 1].score; 
    }
};

static Font chineseFont;
static bool fontLoaded = false;
// 初始化加载中文字体
void InitChineseFont() {
    const char* text = "FPS Physics 分数生命暂停继续重新开始游戏结束胜利排行榜第名按P-暂停按R-重新开始时间倍率落地惩罚恭喜进入空格发射等待加长板多球减速球暂无记录秒 BOOST 按 M 查看排行榜 : !    Breakout - 联机版 + 多线程     加载完成！砖块已变色 L ding... 关卡 1 2 3 4 5 6 7 8 9 0 / 检测到存档，按空格继续 N 切换";

    int codepointCount = 0;
    int* codepoints = LoadCodepoints(text, &codepointCount);

    const char* fontPaths[] = {
        "./fonts/NotoSansSC.otf",
        "../fonts/NotoSansSC.otf",
        "/mnt/d/game111/Breakout/fonts/NotoSansSC.otf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/mnt/c/Windows/Fonts/msyh.ttc"
    };

    printf("正在尝试加载中文字体...\n");
    for (int i = 0; i < 5; i++) {
        if (FileExists(fontPaths[i])) {
            printf("找到字体文件: %s\n", fontPaths[i]);
            chineseFont = LoadFontEx(fontPaths[i], 80, codepoints, codepointCount);
            if (chineseFont.texture.id != 0) {
                printf("✅ 中文字体加载成功！\n");
                fontLoaded = true;
                break;
            }
        }
    }

    if (!fontLoaded) {
        printf("❌ 字体加载失败，使用默认字体\n");
        chineseFont = GetFontDefault();
    }

    UnloadCodepoints(codepoints);
}
// 绘制普通位置中文文本
void DrawChineseText(const char* text, int x, int y, int fontSize, Color color) {
    Vector2 pos = { (float)x, (float)y };
    DrawTextEx(chineseFont, text, pos, 24, 2, color);
}
// 绘制水平居中中文文本
void DrawChineseTextCentered(const char* text, int y, int fontSize, Color color) {
    Vector2 size = MeasureTextEx(chineseFont, text, 24, 2);
    DrawChineseText(text, (GetScreenWidth() - (int)size.x) / 2, y, fontSize, color);
}
// 根据游戏时长计算得分倍率
int CalculateScore(int baseScore, float gameTime) {
    float multiplier = 5.0f - gameTime * 0.05f;
    if (multiplier < 1.0f) multiplier = 1.0f;
    return (int)(baseScore * multiplier);
}
// 挡板、球体尺寸速度常量
static const float DEFAULT_PADDLE_WIDTH = 120.0f;
static const float EXTENDED_PADDLE_WIDTH = 200.0f;
static const float DEFAULT_BALL_MAX_SPEED = 15.0f;
static const float SLOW_BALL_MAX_SPEED = 9.0f;
// 获取道具对应中文名称
const char* GetPowerupName(PowerupType type) {
    switch (type) {
        case PowerupType::ExtendPaddle: return "加长板";
        case PowerupType::MultiBall: return "多球";
        case PowerupType::SlowBall: return "减速球";
        default: return "未知";
    }
}
// 读取道具配置文件
bool LoadPowerupConfigs(std::vector<PowerupConfig>& configs) {
    if (PowerupFactory::LoadConfig("powerups.json", configs)) return true;
    if (PowerupFactory::LoadConfig("../powerups.json", configs)) return true;
    configs.clear();
    configs.push_back({ PowerupType::ExtendPaddle, "加长板", 10.0f, 0.20f, Color{100, 200, 255, 255}, 30.0f });
    configs.push_back({ PowerupType::MultiBall, "多球", 8.0f, 0.15f, Color{255, 220, 120, 255}, 30.0f });
    configs.push_back({ PowerupType::SlowBall, "减速球", 10.0f, 0.15f, Color{120, 255, 180, 255}, 30.0f });
    return true;
}
// 按概率随机选取生成道具
const PowerupConfig* ChoosePowerup(const std::vector<PowerupConfig>& configs) {
    for (const auto& cfg : configs) {
        if ((float)rand() / (float)RAND_MAX < cfg.chance) return &cfg;
    }
    return nullptr;
}
// 执行道具生效效果
void ApplyPowerupEffect(PowerupType type, Paddle& paddle, std::vector<Ball>& balls, std::vector<ActivePowerup>& activeEffects, const std::vector<PowerupConfig>& configs, ParticleSystem& particles) {
    const PowerupConfig* cfg = PowerupFactory::FindConfig(type, configs);
    if (!cfg) return;
    bool refreshed = false;
    for (auto& effect : activeEffects) {
        if (effect.type == type) { effect.remaining = cfg->duration; refreshed = true; break; }
    }
    if (!refreshed) activeEffects.push_back({ type, cfg->duration, true });

    switch (type) {
        case PowerupType::ExtendPaddle:
            paddle.SetWidth(EXTENDED_PADDLE_WIDTH);
            break;
        case PowerupType::MultiBall: {
            if (balls.size() <= 1 && balls.front().IsLaunched()) {
                Ball base = balls.front();
                Vector2 baseSpeed = base.GetSpeed();
                float speedMag = std::sqrt(baseSpeed.x * baseSpeed.x + baseSpeed.y * baseSpeed.y);
                if (speedMag < 1.0f) speedMag = 8.0f;
                for (int i = -1; i <= 1; i += 2) {
                    float angle = std::atan2(baseSpeed.y, baseSpeed.x) + i * 25.0f * 3.14159f / 180.0f;
                    Vector2 sp = { std::cos(angle) * speedMag, std::sin(angle) * speedMag };
                    Ball extra(base.GetPosition(), sp, base.GetRadius());
                    extra.SetMaxSpeed(DEFAULT_BALL_MAX_SPEED);
                    extra.SetLaunched(true);
                    balls.push_back(extra);
                }
            }
            break;
        }
        case PowerupType::SlowBall:
            for (auto& ball : balls) {
                ball.SetSpeed({ ball.GetSpeed().x * 0.65f, ball.GetSpeed().y * 0.65f });
                ball.SetMaxSpeed(SLOW_BALL_MAX_SPEED);
            }
            break;
        default:
            break;
    }

    particles.EmitAura(paddle.GetRect(), cfg->color);
}
// 更新道具剩余生效时间，过期还原状态
void UpdateActiveEffects(float delta, Paddle& paddle, std::vector<Ball>& balls, std::vector<ActivePowerup>& activeEffects) {
    bool stillExtended = false;
    bool stillSlow = false;
    bool stillMulti = false;
    for (auto& effect : activeEffects) effect.remaining -= delta;
    activeEffects.erase(std::remove_if(activeEffects.begin(), activeEffects.end(), [&](const ActivePowerup& effect) {
        if (effect.remaining > 0.0f) {
            if (effect.type == PowerupType::ExtendPaddle) stillExtended = true;
            if (effect.type == PowerupType::SlowBall) stillSlow = true;
            if (effect.type == PowerupType::MultiBall) stillMulti = true;
            return false;
        }
        return true;
    }), activeEffects.end());

    if (!stillExtended) paddle.SetWidth(DEFAULT_PADDLE_WIDTH);
    if (!stillSlow) {
        for (auto& ball : balls) ball.SetMaxSpeed(DEFAULT_BALL_MAX_SPEED);
    }
    if (!stillMulti && balls.size() > 1) {
        balls.erase(balls.begin() + 1, balls.end());
    }
}
// 程序主入口函数
int main(int argc, char* argv[]) {
// 初始化网络库
    if (enet_initialize() != 0) {
        std::cout << "ENet 初始化失败！" << std::endl;
        return 1;
    }
    std::cout << "ENet 初始化成功！" << std::endl;

    if (argc > 1 && std::string(argv[1]) == "host") {
        isHost = true;
        std::cout << "✅ 以主机模式启动\n";
    } else if (argc > 1 && std::string(argv[1]) == "client") {
        isHost = false;
        std::cout << "✅ 以客户端模式启动\n";
    } else {
        std::cout << "用法：./breakout_week2 host|client\n";
        return 1;
    }
// 创建网络监听/连接
    if (isHost) {
        ENetAddress address;
        enet_address_set_host(&address, "0.0.0.0");
        address.port = 1234;
        netHost = enet_host_create(&address, 1, 2, 0, 0);
        std::cout << "✅ 主机已启动，等待客户端连接...\n";
    } else {
        netHost = enet_host_create(NULL, 1, 2, 0, 0);
        ENetAddress address;
        enet_address_set_host(&address, "127.0.0.1");
        address.port = 1234;
        netPeer = enet_host_connect(netHost, &address, 2, 0);
        std::cout << "🔗 正在连接主机...\n";
    }
// 窗口尺寸定义
    const int screenWidth = 800, screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Breakout - 联机版 + 多线程");
    InitChineseFont();
    Leaderboard leaderboard("scores.txt");
// 游戏对象初始化
    std::vector<Ball> balls;
    balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
    Paddle paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
    
    std::vector<Brick> bricks;
    int winCount = 0;
    const int totalLevels = 3;
    int currentLevel = 1;
    int score = 0, lives = 3, playerRank = 0;
// 读取本地存档
    GameSave save;
    if (LoadGame(save)) {
        currentLevel = save.currentLevel;
        score = save.score;
        lives = save.lives;
        EndDrawing();
        // 存档选择弹窗
        while (!IsKeyPressed(KEY_SPACE) && !IsKeyPressed(KEY_R) && !WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawChineseTextCentered("检测到存档，按空格继续", screenHeight/2, 24, YELLOW);
            DrawChineseTextCentered("按 R 重新开始", screenHeight/2 + 40, 24, WHITE);
            EndDrawing();
        }
        if (IsKeyPressed(KEY_R)) {
            remove("save.json");
            currentLevel = 1;
            score = 0;
            lives = 3;
        }
    }
    LoadLevelFromJSON(currentLevel, bricks, winCount);
// 道具、粒子系统初始化
    std::vector<PowerupConfig> powerupConfigs;
    LoadPowerupConfigs(powerupConfigs);
    std::vector<Powerup> powerups;
    ParticleSystem particles;
    std::vector<ActivePowerup> activeEffects;
// 游戏状态标记
    bool gameOver = false, paused = false, victory = false, showLeaderboard = false;
    float gameTime = 0.0f;

    SetTargetFPS(60);
// 性能统计变量
    double totalFrameTime = 0.0;
    double totalPhysicsTime = 0.0;
    int frameCount = 0;
// 游戏主循环
    while (!WindowShouldClose()) {
        double frameStartTime = GetTime();
// 处理网络收发事件
        ENetEvent event;
        while (enet_host_service(netHost, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                std::cout << "✅ 玩家连接成功！" << std::endl;
                netPeer = event.peer;
            }
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                if (isHost) {
                    PaddleInput in = *(PaddleInput*)event.packet->data;
                    remoteState.paddle2X = in.x;
                } else {
                    if (hasLastState) lastState = remoteState;
                    else hasLastState = true;
                    remoteState = *(GameState*)event.packet->data;
                }
                enet_packet_destroy(event.packet);
            }
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                std::cout << "❌ 连接断开！" << std::endl;
                hasLastState = false;
                netPeer = nullptr;
            }
        }
// 主机定时同步游戏状态
        static float sendTimer = 0;
        sendTimer += GetFrameTime();
        if (isHost && netPeer && sendTimer > 1.0f / 30.0f) {
            GameState s{};
            if (!balls.empty()) {
                s.ballX = balls[0].GetPosition().x;
                s.ballY = balls[0].GetPosition().y;
                s.ballSpeedX = balls[0].GetSpeed().x;
                s.ballSpeedY = balls[0].GetSpeed().y;
            }
            s.paddle1X = paddle.GetRect().x;
            s.paddle2X = remoteState.paddle2X;
            s.score = score;
            s.lives = lives;
            s.timestamp = GetTime();

            ENetPacket* packet = enet_packet_create(&s, sizeof(s), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(netPeer, 0, packet);
            sendTimer = 0;
        }

        // ========== N 键跳关 ==========
        if (IsKeyPressed(KEY_N))
        {
            if (currentLevel < totalLevels)
            {
                currentLevel++;
                LoadLevelFromJSON(currentLevel, bricks, winCount);
                score = 0;
                gameTime = 0;
                SaveGame({1, currentLevel, 0, lives});

                balls.clear();
                balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
                paddle = Paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
                powerups.clear();
                activeEffects.clear();
            }
        }
// 客户端上传挡板位置
        if (!isHost) {
            currentInput.x = paddle.GetRect().x;
            ENetPacket* packet = enet_packet_create(&currentInput, sizeof(currentInput), 0);
            enet_peer_send(netPeer, 0, packet);
        }
// 按键功能：暂停、重开、排行榜
        if (IsKeyPressed(KEY_P) && !gameOver) paused = !paused;
        if (IsKeyPressed(KEY_R)) {
            balls.clear();
            balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
            paddle = Paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
            powerups.clear();
            activeEffects.clear();
            particles = ParticleSystem();
            score = 0; lives = 3; gameOver = false; victory = false; paused = false; showLeaderboard = false; gameTime = 0.0f;
            currentLevel = 1;
            LoadLevelFromJSON(currentLevel, bricks, winCount);
            remove("save.json");
        }
        if (IsKeyPressed(KEY_M)) showLeaderboard = !showLeaderboard;
// L键触发异步加载任务
        if (IsKeyPressed(KEY_L) && loadState == LoadState::IDLE) {
            loadState = LoadState::LOADING;
            bricksLoaded = false;
            loadFuture = std::async(std::launch::async, LoadLevelAsync);
        }
// 检测异步加载完成状态
        if (loadState == LoadState::LOADING) {
            auto status = loadFuture.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready) {
                loadState = LoadState::DONE;
                for (auto& brick : bricks) {
                    brick.SetColor(PURPLE);
                }
            }
        }

        float delta = GetFrameTime();
        double physicsStartTime = GetTime();
// 游戏逻辑更新
        if (!gameOver && !paused) {
            if (!balls.empty() && balls.front().IsLaunched()) gameTime += delta;
// 挡板移动控制
            float currentSpeed = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 28.0f : 18.0f;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) paddle.MoveLeft(currentSpeed);
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) paddle.MoveRight(currentSpeed);
// 球体发射与跟随
            if (!balls.empty() && !balls.front().IsLaunched()) {
                balls.front().ResetToPaddle(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().y);
                if (IsKeyPressed(KEY_SPACE)) balls.front().Launch(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().width);
            }
// 球体物理运动更新
            for (auto& ball : balls) {
                ball.ApplyGravity();
                ball.Move();
                ball.BounceEdge(screenWidth, screenHeight);
                ball.BouncePaddle(paddle.GetRect());
            }
// 网格碰撞检测更新
            UpdateBrickGrid(bricks);
            for (auto& ball : balls) {
                Vector2 pos = ball.GetPosition();
                int col = (int)(pos.x / CELL_WIDTH);
                int row = (int)(pos.y / CELL_HEIGHT);

                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        int r = row + dr;
                        int c = col + dc;
                        if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS) {
                            for (auto brick : grid[r][c]) {
                                if (brick->IsActive() && ball.CheckBrickCollision(brick->GetRect())) {
                                    brick->SetActive(false);
                                    score += CalculateScore(10, gameTime);
                                    winCount--;
                                    particles.EmitExplosion({ brick->GetRect().x + brick->GetRect().width / 2, brick->GetRect().y + brick->GetRect().height / 2 }, brick->GetColor(), 16);
                                    const PowerupConfig* cfg = ChoosePowerup(powerupConfigs);
                                    if (cfg) {
                                        powerups.push_back(PowerupFactory::Create(*cfg, brick->GetRect().x + brick->GetRect().width / 2, brick->GetRect().y + brick->GetRect().height / 2));
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // ========== 500 分自动进下一关 ==========
            if (score >= 500 && currentLevel < totalLevels && !gameOver && !paused)
            {
                currentLevel++;
                LoadLevelFromJSON(currentLevel, bricks, winCount);
                score = 0;
                gameTime = 0;
                SaveGame({1, currentLevel, 0, lives});

                balls.clear();
                balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
                paddle = Paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
                powerups.clear();
                activeEffects.clear();
            }

            // 通关切换关卡判断
            if (winCount <= 0) {
                if (currentLevel < totalLevels) {
                    currentLevel++;
                    LoadLevelFromJSON(currentLevel, bricks, winCount);
                    GameSave newSave;
                    newSave.currentLevel = currentLevel;
                    newSave.score = score;
                    newSave.lives = lives;
                    SaveGame(newSave);
                    balls.clear();
                    balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
                    paddle = Paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
                    powerups.clear();
                    activeEffects.clear();
                    gameTime = 0.0f;
                } else {
                    gameOver = true; victory = true;
                    if (leaderboard.CanEnter(score)) playerRank = leaderboard.AddScore("Player", score);
                    remove("save.json");
                }
            }
             // 剔除出界球体
            std::vector<Ball> remainingBalls;
            for (auto& ball : balls) {
                if (ball.GetPosition().y <= screenHeight + 50) remainingBalls.push_back(ball);
            }
            // 球体全部掉落扣除生命
            if (remainingBalls.empty()) {
                lives--;
                score -= 50;
                if (score < 0) score = 0;
                GameSave newSave;
                newSave.currentLevel = currentLevel;
                newSave.score = score;
                newSave.lives = lives;
                SaveGame(newSave);

                if (lives <= 0) {
                    gameOver = true;
                    victory = false;
                    if (leaderboard.CanEnter(score)) playerRank = leaderboard.AddScore("Player", score);
                    remove("save.json");
                } else {
                    remainingBalls.emplace_back(Vector2{paddle.GetRect().x + paddle.GetRect().width / 2, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
                    remainingBalls.front().ResetToPaddle(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().y);
                }
            }
            balls = std::move(remainingBalls);
// 道具更新与拾取判定
            for (auto& powerup : powerups) {
                powerup.Update(delta);
                if (powerup.CheckCatch(paddle.GetRect())) {
                    ApplyPowerupEffect(powerup.GetType(), paddle, balls, activeEffects, powerupConfigs, particles);
                }
            }
            powerups.erase(std::remove_if(powerups.begin(), powerups.end(), [](const Powerup& powerup) { return !powerup.IsAlive(); }), powerups.end());
// 道具时效、粒子动画更新
            UpdateActiveEffects(delta, paddle, balls, activeEffects);
            particles.Update(delta);
        }
        
        double physicsElapsed = GetTime() - physicsStartTime;
        totalPhysicsTime += physicsElapsed;
// 画面渲染绘制
        BeginDrawing();
        ClearBackground(Color{30, 30, 40, 255});
// 绘制游戏边界墙
        DrawRectangle(0, 0, 5, screenHeight, GRAY);
        DrawRectangle(screenWidth - 5, 0, 5, screenHeight, GRAY);
        DrawRectangle(0, 0, screenWidth, 5, GRAY);
// 绘制场景元素
        for (auto& brick : bricks) brick.Draw();
        for (auto& powerup : powerups) powerup.Draw(chineseFont);
        particles.Draw();
// 主机客户端画面区分绘制
        if (isHost) {
            paddle.Draw();
            for (auto& ball : balls) ball.Draw();
            DrawRectangle(remoteState.paddle2X, 550, DEFAULT_PADDLE_WIDTH, 15, RED);
        } else {
            paddle.Draw();
            DrawRectangle(remoteState.paddle1X, 550, DEFAULT_PADDLE_WIDTH, 15, BLUE);
// 球体位置插值平滑
            if (hasLastState) {
                double now = GetTime();
                float t = (now - lastState.timestamp) / (remoteState.timestamp - lastState.timestamp + 0.001f);
                t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
                float drawBallX = lastState.ballX * (1 - t) + remoteState.ballX * t;
                float drawBallY = lastState.ballY * (1 - t) + remoteState.ballY * t;
                DrawCircle(drawBallX, drawBallY, 10, WHITE);
            } else {
                DrawCircle(remoteState.ballX, remoteState.ballY, 10, WHITE);
            }
        }
// 异步加载状态提示文字
        if (loadState == LoadState::LOADING) {
            DrawChineseTextCentered("Loading...", screenHeight / 2, 30, YELLOW);
        } else if (loadState == LoadState::DONE) {
            DrawChineseTextCentered("加载完成！砖块已变色", screenHeight / 2, 24, GREEN);
        }
// 游戏信息UI绘制
         char levelStr[64];
         sprintf(levelStr, "关卡: %d / %d", currentLevel, totalLevels);
         DrawChineseText(levelStr, 20, 15, 22, WHITE);

         char scoreStr[64];
         sprintf(scoreStr, "分数: %d / 500", score);
         DrawChineseText(scoreStr, 20, 45, 22, YELLOW);

         char livesStr[64];
         sprintf(livesStr, "生命: %d", lives);
         DrawChineseText(livesStr, 20, 75, 22, GREEN);

         char timeStr[64];
         sprintf(timeStr, "时间: %.1f 秒", gameTime);
         DrawChineseText(timeStr, 20, 105, 20, SKYBLUE);

        float currentMultiplier = 5.0f - gameTime * 0.05f;
        if (currentMultiplier < 1.0f) currentMultiplier = 1.0f;
        DrawText(TextFormat("x%.1f", currentMultiplier), 140, 105, 20, currentMultiplier > 2.0f ? GREEN : LIGHTGRAY);
// 生效道具提示
        int effectY = 135;
        for (const auto& effect : activeEffects) {
            DrawChineseText(TextFormat("%s: %.0fs", GetPowerupName(effect.type), effect.remaining), 20, effectY, 18, SKYBLUE);
            effectY += 22;
        }
// 操作提示文本
        if (!balls.empty() && !balls.front().IsLaunched()) DrawChineseTextCentered("按空格发射", 55, 20, YELLOW);
        DrawChineseTextCentered("按 M 查看排行榜", 40, 20, Fade(WHITE, 0.7f));
        DrawChineseText("P-暂停 R-重开 M-排行 L-加载 N-切换关卡", 280, 12, 18, Fade(WHITE, 0.6f));
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) DrawChineseText(">>> BOOST <<<", 350, 575, 18, YELLOW);
// 暂停界面绘制
        if (paused && !gameOver) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.7f));
            DrawChineseTextCentered("暂停", screenHeight/2 - 40, 48, YELLOW);
            DrawChineseTextCentered("按 P 继续", screenHeight/2 + 30, 24, WHITE);
        }
// 游戏结束结算界面
      if (gameOver) {
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.85f));

    if (victory) {
        const char* title = "胜利!";
        int titleFontSize = 80;
        Vector2 titleSize = MeasureTextEx(chineseFont, title, titleFontSize, 2);
        float titleX = (screenWidth - titleSize.x) / 2;
        float titleY = screenHeight / 2 - 180;

        DrawTextEx(chineseFont, title, (Vector2){titleX + 3, titleY + 3}, titleFontSize, 2, BLACK);
        DrawTextEx(chineseFont, title, (Vector2){titleX, titleY}, titleFontSize, 2, GREEN);
    } else {
        const char* title = "游戏结束";
        int titleFontSize = 100;
        Vector2 titleSize = MeasureTextEx(chineseFont, title, titleFontSize, 2);
        float titleX = (screenWidth - titleSize.x) / 2;
        float titleY = screenHeight / 2 - 180;

        DrawTextEx(chineseFont, title, (Vector2){titleX + 3, titleY + 3}, titleFontSize, 2, BLACK);
        DrawTextEx(chineseFont, title, (Vector2){titleX, titleY}, titleFontSize, 2, RED);
    }

    char scoreBuf[64];
    sprintf(scoreBuf, "分数: %d", score);
    int scoreFontSize = 36;
    Vector2 scoreSize = MeasureTextEx(chineseFont, scoreBuf, scoreFontSize, 2);
    float scoreX = (screenWidth - scoreSize.x) / 2;
    float scoreY = screenHeight / 2 - 60;
    DrawTextEx(chineseFont, scoreBuf, (Vector2){scoreX + 2, scoreY + 2}, scoreFontSize, 2, BLACK);
    DrawTextEx(chineseFont, scoreBuf, (Vector2){scoreX, scoreY}, scoreFontSize, 2, YELLOW);

    if (playerRank > 0) {
        char rankBuf[64];
        sprintf(rankBuf, "恭喜进入排行榜第 %d 名!", playerRank);
        int rankFontSize = 30;
        Vector2 rankSize = MeasureTextEx(chineseFont, rankBuf, rankFontSize, 2);
        float rankX = (screenWidth - rankSize.x) / 2;
        float rankY = screenHeight / 2 + 20;
        DrawTextEx(chineseFont, rankBuf, (Vector2){rankX + 2, rankY + 2}, rankFontSize, 2, BLACK);
        DrawTextEx(chineseFont, rankBuf, (Vector2){rankX, rankY}, rankFontSize, 2, GOLD);
    }

    const char* restart = "按 R 重新开始";
    int restartFontSize = 26;
    Vector2 restartSize = MeasureTextEx(chineseFont, restart, restartFontSize, 2);
    float restartX = (screenWidth - restartSize.x) / 2;
    float restartY = screenHeight / 2 + 100;
    DrawTextEx(chineseFont, restart, (Vector2){restartX + 2, restartY + 2}, restartFontSize, 2, BLACK);
    DrawTextEx(chineseFont, restart, (Vector2){restartX, restartY}, restartFontSize, 2, WHITE);
}
// 排行榜界面绘制
        if (showLeaderboard) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.9f));
            DrawChineseTextCentered("排行榜", 40, 36, GOLD);
            DrawText("RANK", 150, 90, 20, Fade(WHITE, 0.6f));
            DrawText("NAME", 250, 90, 20, Fade(WHITE, 0.6f));
            DrawText("SCORE", 450, 90, 20, Fade(WHITE, 0.6f));
            DrawText("DATE", 550, 90, 20, Fade(WHITE, 0.6f));
            DrawLine(150, 115, 700, 115, Fade(WHITE, 0.3f));
            for (int i = 0; i < leaderboard.GetCount() && i < 10; i++) {
                ScoreEntry entry;
                if (leaderboard.GetEntry(i + 1, entry)) {
                    int y = 130 + i * 35;
                    Color rowColor = (i == 0) ? GOLD : (i == 1) ? LIGHTGRAY : (i == 2) ? ORANGE : WHITE;
                    DrawText(TextFormat("#%d", i + 1), 150, y, 22, rowColor);
                    DrawText(entry.name, 250, y, 22, rowColor);
                    DrawText(TextFormat("%d", entry.score), 450, y, 22, rowColor);
                    char dateStr[32];
                    strftime(dateStr, sizeof(dateStr), "%m/%d", localtime(&entry.timestamp));
                    DrawText(dateStr, 550, y, 20, Fade(rowColor, 0.7f));
                }
            }
            if (leaderboard.GetCount() == 0) DrawChineseTextCentered("暂无记录", screenHeight/2, 24, Fade(WHITE, 0.5f));
            DrawChineseTextCentered("按 M 关闭排行榜", screenHeight - 50, 20, Fade(WHITE, 0.5f));
        }
// 性能帧率显示
        float currentFPS = 1.0f / GetFrameTime();
        DrawText(TextFormat("FPS: %.1f", currentFPS), 20, 550, 20, GREEN);
        DrawText(TextFormat("Physics: %.2f ms", physicsElapsed * 1000), 20, 575, 20, YELLOW);
        
        EndDrawing();
// 帧耗时统计
        double frameElapsed = GetTime() - frameStartTime;
        totalFrameTime += frameElapsed;
        frameCount++;
// 每30帧打印平均性能
        if (frameCount % 30 == 0) {
            double avgFPS = frameCount / totalFrameTime;
            double avgPhysics = totalPhysicsTime / frameCount * 1000;
            //printf("【优化后】平均FPS: %.2f | 物理耗时: %.2f ms\n", avgFPS, avgPhysics);
        }
    }
// 输出最终性能统计

    if (frameCount > 0) {
        double avgFPS = frameCount / totalFrameTime;
        double avgPhysics = totalPhysicsTime / frameCount * 1000;
        //printf("\n===== 优化后最终测试结果 =====\n");
        //printf("平均 FPS: %.2f\n", avgFPS);
        //printf("平均物理/碰撞耗时: %.2f ms\n", avgPhysics);
    }
// 释放全局资源
    if (netHost) enet_host_destroy(netHost);
    enet_deinitialize();
    TextureCache::getInstance().clear();

    CloseWindow();
    return 0;
}