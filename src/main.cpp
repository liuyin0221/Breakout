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

// ===================== 联机核心结构体 =====================
#pragma pack(push, 1)
struct GameState {
    float ballX, ballY;
    float ballSpeedX, ballSpeedY;
    float paddle1X;
    float paddle2X;
    int score;
    int lives;
    double timestamp;
};

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

// ===================== 本周任务：多线程相关 =====================
enum class LoadState { IDLE, LOADING, DONE };
LoadState loadState = LoadState::IDLE;
std::future<void> loadFuture;
std::mutex loadMutex;
bool bricksLoaded = false; // 共享状态：砖块是否加载完成

// 线程安全的纹理缓存（加分项）
class TextureCache {
private:
    std::unordered_map<std::string, Texture2D> cache;
    mutable std::mutex mtx;

    TextureCache() = default;
public:
    static TextureCache& getInstance() {
        static TextureCache instance;
        return instance;
    }

    Texture2D get(const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(path);
        if (it != cache.end()) return it->second;
        
        // 实际项目中这里用 LoadTexture，我们这里用占位
        Texture2D tex = LoadTexture(path.c_str());
        cache[path] = tex;
        return tex;
    }

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
    // 模拟耗时加载（sleep代替大型纹理加载）
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 加载完成后修改共享状态
    std::lock_guard<std::mutex> lock(loadMutex);
    bricksLoaded = true;
}

// ===================== 你的原有代码开始 =====================
struct ScoreEntry {
    char name[32];
    int score;
    time_t timestamp;
};

class Leaderboard {
private:
    static const int MAX_ENTRIES = 10;
    ScoreEntry entries[MAX_ENTRIES];
    int count;
    const char* filename;
    
public:
    Leaderboard(const char* file) : count(0), filename(file) { Load(); }
    
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
    
    void Save() {
        FILE* f = fopen(filename, "w");
        if (f) {
            for (int i = 0; i < count; i++) fprintf(f, "%s %d %ld\n", entries[i].name, entries[i].score, entries[i].timestamp);
            fclose(f);
        }
    }
    
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
    
    int GetRank(int index) {
        if (index < 0 || index >= count) return 0;
        int rank = 1;
        for (int i = 0; i < index; i++) {
            if (entries[i].score > entries[index].score) rank++;
        }
        return rank;
    }

    bool GetEntry(int rank, ScoreEntry& entry) { if (rank > 0 && rank <= count) { entry = entries[rank - 1]; return true; } return false; }
    int GetCount() { return count; }
    bool CanEnter(int score) { return count < MAX_ENTRIES || score > entries[count - 1].score; }
};

static Font chineseFont;
static bool fontLoaded = false;

void InitChineseFont() {
    const char* text = "分数生命暂停继续重新开始游戏结束胜利排行榜第名按P-暂停按R-重新开始时间倍率落地惩罚恭喜进入空格发射等待加长板多球减速球暂无记录秒 BOOST 按 M 查看排行榜 : !    Breakout - 联机版 + 多线程     加载完成！砖块已变色 L ding...";

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
            chineseFont = LoadFontEx(fontPaths[i], 24, codepoints, codepointCount);
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

void DrawChineseText(const char* text, int x, int y, int fontSize, Color color) {
    Vector2 pos = { (float)x, (float)y };
    DrawTextEx(chineseFont, text, pos, 24, 2, color);
}

void DrawChineseTextCentered(const char* text, int y, int fontSize, Color color) {
    Vector2 size = MeasureTextEx(chineseFont, text, 24, 2);
    DrawChineseText(text, (GetScreenWidth() - (int)size.x) / 2, y, fontSize, color);
}

int CalculateScore(int baseScore, float gameTime) {
    float multiplier = 5.0f - gameTime * 0.05f;
    if (multiplier < 1.0f) multiplier = 1.0f;
    return (int)(baseScore * multiplier);
}

static const float DEFAULT_PADDLE_WIDTH = 120.0f;
static const float EXTENDED_PADDLE_WIDTH = 200.0f;
static const float DEFAULT_BALL_MAX_SPEED = 15.0f;
static const float SLOW_BALL_MAX_SPEED = 9.0f;

const char* GetPowerupName(PowerupType type) {
    switch (type) {
        case PowerupType::ExtendPaddle: return "加长板";
        case PowerupType::MultiBall: return "多球";
        case PowerupType::SlowBall: return "减速球";
        default: return "未知";
    }
}

bool LoadPowerupConfigs(std::vector<PowerupConfig>& configs) {
    if (PowerupFactory::LoadConfig("powerups.json", configs)) return true;
    if (PowerupFactory::LoadConfig("../powerups.json", configs)) return true;
    configs.clear();
    configs.push_back({ PowerupType::ExtendPaddle, "加长板", 10.0f, 0.20f, Color{100, 200, 255, 255}, 30.0f });
    configs.push_back({ PowerupType::MultiBall, "多球", 8.0f, 0.15f, Color{255, 220, 120, 255}, 30.0f });
    configs.push_back({ PowerupType::SlowBall, "减速球", 10.0f, 0.15f, Color{120, 255, 180, 255}, 30.0f });
    return true;
}

const PowerupConfig* ChoosePowerup(const std::vector<PowerupConfig>& configs) {
    for (const auto& cfg : configs) {
        if ((float)rand() / (float)RAND_MAX < cfg.chance) return &cfg;
    }
    return nullptr;
}

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

int main(int argc, char* argv[]) {
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

    const int screenWidth = 800, screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Breakout - 联机版 + 多线程");
    InitChineseFont();
    Leaderboard leaderboard("scores.txt");
    
    std::vector<Ball> balls;
    balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
    Paddle paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
    
    std::vector<Brick> bricks;
    Color brickColors[] = {RED, ORANGE, YELLOW, GREEN, BLUE};
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 8; col++) {
            bricks.emplace_back(50 + col * 95, 80 + row * 35, 85, 25, brickColors[row]);
        }
    }

    std::vector<PowerupConfig> powerupConfigs;
    LoadPowerupConfigs(powerupConfigs);
    std::vector<Powerup> powerups;
    ParticleSystem particles;
    std::vector<ActivePowerup> activeEffects;

    int score = 0, lives = 3, winCount = (int)bricks.size(), playerRank = 0;
    bool gameOver = false, paused = false, victory = false, showLeaderboard = false;
    float gameTime = 0.0f;

    SetTargetFPS(60);
    
    while (!WindowShouldClose()) {
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

        if (!isHost) {
            currentInput.x = paddle.GetRect().x;
            ENetPacket* packet = enet_packet_create(&currentInput, sizeof(currentInput), 0);
            enet_peer_send(netPeer, 0, packet);
        }

        if (IsKeyPressed(KEY_P) && !gameOver) paused = !paused;
        if (IsKeyPressed(KEY_R)) {
            balls.clear();
            balls.emplace_back(Vector2{400.0f, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
            paddle = Paddle(340.0f, 550.0f, DEFAULT_PADDLE_WIDTH, 15.0f);
            powerups.clear();
            activeEffects.clear();
            particles = ParticleSystem();
            score = 0; lives = 3; gameOver = false; victory = false; paused = false; showLeaderboard = false; gameTime = 0.0f;
            bricks.clear();
            for (int row = 0; row < 5; row++) for (int col = 0; col < 8; col++) bricks.emplace_back(50 + col * 95, 80 + row * 35, 85, 25, brickColors[row]);
            winCount = (int)bricks.size();
            // 重置加载状态
            loadState = LoadState::IDLE;
            bricksLoaded = false;
        }
        if (IsKeyPressed(KEY_M)) showLeaderboard = !showLeaderboard;

        // ===================== 本周任务：异步加载控制 =====================
        if (IsKeyPressed(KEY_L) && loadState == LoadState::IDLE) {
            loadState = LoadState::LOADING;
            bricksLoaded = false;
            loadFuture = std::async(std::launch::async, LoadLevelAsync);
        }

        // 检查加载是否完成
        if (loadState == LoadState::LOADING) {
            auto status = loadFuture.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready) {
                loadState = LoadState::DONE;
                // 加载完成后修改砖块颜色（作为反馈）
                for (auto& brick : bricks) {
                    brick.SetColor(PURPLE);
                }
            }
        }

        float delta = GetFrameTime();
        if (!gameOver && !paused) {
            if (!balls.empty() && balls.front().IsLaunched()) gameTime += delta;

            // ===================== 双人分离控制逻辑 =====================
            float currentSpeed = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 28.0f : 18.0f;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) paddle.MoveLeft(currentSpeed);
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) paddle.MoveRight(currentSpeed);

            if (!balls.empty() && !balls.front().IsLaunched()) {
                balls.front().ResetToPaddle(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().y);
                if (IsKeyPressed(KEY_SPACE)) balls.front().Launch(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().width);
            }

            for (auto& ball : balls) {
                ball.ApplyGravity();
                ball.Move();
                ball.BounceEdge(screenWidth, screenHeight);
                ball.BouncePaddle(paddle.GetRect());
            }

            for (auto& ball : balls) {
                for (auto& brick : bricks) {
                    if (brick.IsActive() && ball.CheckBrickCollision(brick.GetRect())) {
                        brick.SetActive(false);
                        score += CalculateScore(10, gameTime);
                        winCount--;
                        particles.EmitExplosion({ brick.GetRect().x + brick.GetRect().width / 2, brick.GetRect().y + brick.GetRect().height / 2 }, brickColors[0], 16);
                        const PowerupConfig* cfg = ChoosePowerup(powerupConfigs);
                        if (cfg) {
                            powerups.push_back(PowerupFactory::Create(*cfg, brick.GetRect().x + brick.GetRect().width / 2, brick.GetRect().y + brick.GetRect().height / 2));
                        }
                        break;
                    }
                }
            }

            if (winCount <= 0) {
                gameOver = true; victory = true;
                if (leaderboard.CanEnter(score)) playerRank = leaderboard.AddScore("Player", score);
            }

            std::vector<Ball> remainingBalls;
            for (auto& ball : balls) {
                if (ball.GetPosition().y <= screenHeight + 50) remainingBalls.push_back(ball);
            }
            if (remainingBalls.empty()) {
                lives--;
                score -= 50;
                if (score < 0) score = 0;
                if (lives <= 0) {
                    gameOver = true;
                    victory = false;
                    if (leaderboard.CanEnter(score)) playerRank = leaderboard.AddScore("Player", score);
                } else {
                    remainingBalls.emplace_back(Vector2{paddle.GetRect().x + paddle.GetRect().width / 2, 530.0f}, Vector2{0.0f, 0.0f}, 10.0f);
                    remainingBalls.front().ResetToPaddle(paddle.GetRect().x + paddle.GetRect().width / 2, paddle.GetRect().y);
                }
            }
            balls = std::move(remainingBalls);

            for (auto& powerup : powerups) {
                powerup.Update(delta);
                if (powerup.CheckCatch(paddle.GetRect())) {
                    ApplyPowerupEffect(powerup.GetType(), paddle, balls, activeEffects, powerupConfigs, particles);
                }
            }
            powerups.erase(std::remove_if(powerups.begin(), powerups.end(), [](const Powerup& powerup) { return !powerup.IsAlive(); }), powerups.end());

            UpdateActiveEffects(delta, paddle, balls, activeEffects);
            particles.Update(delta);
        }

        BeginDrawing();
        ClearBackground(Color{30, 30, 40, 255});
        DrawRectangle(0, 0, 5, screenHeight, GRAY);
        DrawRectangle(screenWidth - 5, 0, 5, screenHeight, GRAY);
        DrawRectangle(0, 0, screenWidth, 5, GRAY);

        for (auto& brick : bricks) brick.Draw();
        for (auto& powerup : powerups) powerup.Draw(chineseFont);
        particles.Draw();

        // ===================== 双人分离绘制逻辑 =====================
        if (isHost) {
            paddle.Draw();
            for (auto& ball : balls) ball.Draw();
            DrawRectangle(remoteState.paddle2X, 550, DEFAULT_PADDLE_WIDTH, 15, RED);
        } else {
            paddle.Draw();
            DrawRectangle(remoteState.paddle1X, 550, DEFAULT_PADDLE_WIDTH, 15, BLUE);

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

        // 本周任务：绘制Loading动画
        if (loadState == LoadState::LOADING) {
            DrawChineseTextCentered("Loading...", screenHeight / 2, 30, YELLOW);
        } else if (loadState == LoadState::DONE) {
            DrawChineseTextCentered("加载完成！砖块已变色", screenHeight / 2, 24, GREEN);
        }

        DrawChineseText("分数:", 20, 8, 24, WHITE);
        DrawText(TextFormat("%d", isHost ? score : remoteState.score), 80, 10, 24, YELLOW);
        DrawChineseText("生命:", 650, 8, 24, WHITE);
        DrawText(TextFormat("%d", isHost ? lives : remoteState.lives), 710, 10, 24, lives > 1 ? GREEN : RED);
        DrawChineseText("时间:", 20, 35, 20, Fade(WHITE, 0.8f));
        DrawText(TextFormat("%.1f", gameTime), 75, 37, 20, Fade(WHITE, 0.8f));

        float currentMultiplier = 5.0f - gameTime * 0.05f;
        if (currentMultiplier < 1.0f) currentMultiplier = 1.0f;
        DrawText(TextFormat("x%.1f", currentMultiplier), 140, 37, 20, currentMultiplier > 2.0f ? GREEN : Fade(WHITE, 0.5f));

        int effectY = 60;
        for (const auto& effect : activeEffects) {
            DrawChineseText(TextFormat("%s: %.0fs", GetPowerupName(effect.type), effect.remaining), 20, effectY, 18, SKYBLUE);
            effectY += 22;
        }

        if (!balls.empty() && !balls.front().IsLaunched()) DrawChineseTextCentered("按空格发射", 55, 20, YELLOW);
        DrawChineseTextCentered("按 M 查看排行榜", 40, 20, Fade(WHITE, 0.7f));
        DrawChineseText("P-暂停 R-重开 M-排行 L-加载", 280, 12, 18, Fade(WHITE, 0.6f));
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) DrawChineseText(">>> BOOST <<<", 350, 575, 18, YELLOW);

        if (paused && !gameOver) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.7f));
            DrawChineseTextCentered("暂停", screenHeight/2 - 40, 48, YELLOW);
            DrawChineseTextCentered("按 P 继续", screenHeight/2 + 30, 24, WHITE);
        }

        if (gameOver) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.85f));
            if (victory) {
                DrawChineseTextCentered("胜利!", screenHeight/2 - 80, 48, GREEN);
                DrawText(TextFormat("FINAL SCORE: %d", score), screenWidth/2 - 100, screenHeight/2 - 30, 28, YELLOW);
            } else {
                DrawChineseTextCentered("游戏结束", screenHeight/2 - 80, 48, RED);
                DrawText(TextFormat("SCORE: %d", score), screenWidth/2 - 60, screenHeight/2 - 30, 28, YELLOW);
            }
            if (playerRank > 0) {
               DrawChineseTextCentered("恭喜进入排行榜第",screenHeight/2+40,24,GOLD);
               DrawText(TextFormat("%d名!",playerRank),screenWidth/2+70,screenHeight/2+40,24,GOLD);
            }
            DrawChineseTextCentered("按 R 重新开始", screenHeight/2 + 90, 24, WHITE);
        }

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
                    Color rowColor = (i == 0) ? GOLD : (i == 1) ? Color{192,192,192,255} : (i == 2) ? Color{205,127,50,255} : WHITE;
                    DrawText(TextFormat("#%d", i + 1), 150, y, 22, rowColor);
                    DrawText(entry.name, 250, y, 22, rowColor);
                    DrawText(TextFormat("%d", entry.score), 450, y, 22, rowColor);
                    char dateStr[32]; strftime(dateStr, sizeof(dateStr), "%m/%d", localtime(&entry.timestamp));
                    DrawText(dateStr, 550, y, 20, Fade(rowColor, 0.7f));
                }
            }
            if (leaderboard.GetCount() == 0) DrawChineseTextCentered("暂无记录", screenHeight/2, 24, Fade(WHITE, 0.5f));
            DrawChineseTextCentered("按 M 关闭排行榜", screenHeight - 50, 20, Fade(WHITE, 0.5f));
        }
        EndDrawing();
    }

    if (netHost) enet_host_destroy(netHost);
    enet_deinitialize();
    TextureCache::getInstance().clear();

    CloseWindow();
    return 0;
}