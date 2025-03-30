
#include <SDL.h>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <cstring>

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 800;
const int GRID_SIZE = 40;
const int MAP_ROWS = SCREEN_HEIGHT / GRID_SIZE;
const int MAP_COLS = SCREEN_WIDTH / GRID_SIZE;

enum GameState {
    STATE_MENU,
    STATE_1P,
    STATE_2P,
    STATE_GAME_OVER
};

enum PowerUpType {
    POWERUP_NONE,
    POWERUP_HEALTH,
    POWERUP_FREEZE,
    POWERUP_INVINCIBLE,
    POWERUP_BOMB
};

class PowerUp {
public:
    SDL_Rect rect;
    PowerUpType type;
    bool active;
    Uint32 spawnTime;
    const Uint32 duration = 10000;
    SDL_Texture* texture;

    PowerUp() : type(POWERUP_NONE), active(false), texture(nullptr) {
        rect = {0, 0, GRID_SIZE, GRID_SIZE};
    }

    void spawn(int x, int y, PowerUpType t) {
        rect.x = x;
        rect.y = y;
        type = t;
        active = true;
        spawnTime = SDL_GetTicks();
    }

    void update() {
        if (!active) return;
        if (SDL_GetTicks() - spawnTime > duration) {
            active = false;
        }
    }

    void render(SDL_Renderer* renderer) {
        if (!active) return;
        if (texture) {
            SDL_RenderCopy(renderer, texture, nullptr, &rect);
        } else {
            // Fallback to colored rectangles if texture is missing
            switch (type) {
                case POWERUP_HEALTH: SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255); break;
                case POWERUP_FREEZE: SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255); break;
                case POWERUP_INVINCIBLE: SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); break;
                case POWERUP_BOMB: SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255); break;
                default: return;
            }
            SDL_RenderFillRect(renderer, &rect);
        }
    }
};

class Bullet {
public:
    SDL_Rect rect;
    int dx, dy;
    bool active;
    SDL_Renderer* renderer;
    SDL_Texture* bulletTexture;

    Bullet(SDL_Renderer* rend, int x, int y, int direction) : renderer(rend), active(true), bulletTexture(nullptr) {
        rect = {x, y, 10, 10};
        dx = (direction == 1 || direction == 3) ? 5 * (direction == 1 ? -1 : 1) : 0;
        dy = (direction == 0 || direction == 2) ? 5 * (direction == 0 ? -1 : 1) : 0;

        SDL_Surface* surface = IMG_Load("bullet.png");
        if (!surface) {
            std::cerr << "Failed to load bullet texture: " << IMG_GetError() << std::endl;
        } else {
            bulletTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    ~Bullet() {
        if (bulletTexture) SDL_DestroyTexture(bulletTexture);
    }

    void update(std::vector<SDL_Rect>& walls, std::vector<bool>& wallBreakable) {
        if (!active) return;
        rect.x += dx;
        rect.y += dy;
        for (const auto& wall : walls) {
            if (SDL_HasIntersection(&rect, &wall)) {
                active = false;
                return;
            }
        }
        if (rect.x < 0 || rect.x > SCREEN_WIDTH || rect.y < 0 || rect.y > SCREEN_HEIGHT) {
            active = false;
        }
    }

    void render() {
        if (!active || !bulletTexture) return;
        SDL_RenderCopy(renderer, bulletTexture, nullptr, &rect);
    }
};

class PlayerTank {
public:
    SDL_Renderer* renderer;
    std::vector<Bullet> bullets;
    int direction; // 0: up, 1: left, 2: down, 3: right
    bool alive;
    float x, y;
    float speed;
    const int width = GRID_SIZE;
    const int height = GRID_SIZE;
    bool keys[4]; // up, left, down, right
    bool invincible;
    Uint32 invincibleEndTime;
    const int maxHealth = 1000;
    int health;
    SDL_Texture* tankTexture;
    SDL_Rect rect;
    Mix_Chunk* shootSound;

    PlayerTank(SDL_Renderer* rend, int startX, int startY, Mix_Chunk* sound) : renderer(rend), alive(true),
        invincible(false), invincibleEndTime(0), health(maxHealth), tankTexture(nullptr), shootSound(sound) {
        x = static_cast<float>(startX);
        y = static_cast<float>(startY);
        rect = {startX, startY, width, height};
        direction = 0;
        speed = 3.0f;
        keys[0] = keys[1] = keys[2] = keys[3] = false;

        SDL_Surface* surface = IMG_Load("tank.png");
        if (!surface) {
            std::cerr << "Failed to load player tank texture: " << IMG_GetError() << std::endl;
        } else {
            tankTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    ~PlayerTank() {
        if (tankTexture) SDL_DestroyTexture(tankTexture);
    }

    void heal(int amount = 200) {
        if (health < maxHealth) {
            health = std::min(maxHealth, health + amount);
        }
    }

    void takeDamage() {
        if (!invincible) {
            health -= 100;
            if (health <= 0) {
                alive = false;
            }
        }
    }

    void activateInvincible(Uint32 duration) {
        if (!invincible) {
            invincible = true;
            invincibleEndTime = SDL_GetTicks() + duration;
        }
    }

    void updateInvincible() {
        if (invincible && SDL_GetTicks() > invincibleEndTime) {
            invincible = false;
        }
    }

    bool checkPowerUpCollision(const SDL_Rect& powerUpRect) {
        return SDL_HasIntersection(&rect, &powerUpRect);
    }

    void handleInput(SDL_Event& event, bool isPlayer1) {
        if (!alive) return;

        bool keyDown = (event.type == SDL_KEYDOWN);

        if (isPlayer1) {
            switch (event.key.keysym.sym) {
                case SDLK_UP: keys[0] = keyDown; break;
                case SDLK_LEFT: keys[1] = keyDown; break;
                case SDLK_DOWN: keys[2] = keyDown; break;
                case SDLK_RIGHT: keys[3] = keyDown; break;
                case SDLK_SPACE: if (keyDown) shoot(); break;
            }
        } else {
            switch (event.key.keysym.sym) {
                case SDLK_w: keys[0] = keyDown; break;
                case SDLK_a: keys[1] = keyDown; break;
                case SDLK_s: keys[2] = keyDown; break;
                case SDLK_d: keys[3] = keyDown; break;
                case SDLK_RETURN: if (keyDown) shoot(); break;
            }
        }
    }

    void update(const std::vector<SDL_Rect>& walls, const SDL_Rect* otherPlayerRect = nullptr) {
        if (!alive) return;

        float newX = x;
        float newY = y;

        if (keys[0]) { // Up
            newY -= speed;
            direction = 0;
        }
        if (keys[2]) { // Down
            newY += speed;
            direction = 2;
        }
        if (keys[1]) { // Left
            newX -= speed;
            direction = 1;
        }
        if (keys[3]) { // Right
            newX += speed;
            direction = 3;
        }

        // Kiểm tra va chạm
        SDL_Rect newRect = {static_cast<int>(newX), static_cast<int>(newY), width, height};
        bool canMove = true;

        for (const auto& wall : walls) {
            if (SDL_HasIntersection(&newRect, &wall)) {
                canMove = false;
                break;
            }
        }

        if (otherPlayerRect && SDL_HasIntersection(&newRect, otherPlayerRect)) {
            canMove = false;
        }

        if (canMove) {
            x = newX;
            y = newY;
            rect.x = static_cast<int>(x);
            rect.y = static_cast<int>(y);
        }

        // Giới hạn trong màn hình
        if (rect.x < 0) rect.x = x = 0;
        if (rect.y < 0) rect.y = y = 0;
        if (rect.x > SCREEN_WIDTH - width) rect.x = x = SCREEN_WIDTH - width;
        if (rect.y > SCREEN_HEIGHT - height) rect.y = y = SCREEN_HEIGHT - height;

        updateInvincible();
    }

    void shoot() {
        bullets.emplace_back(renderer, rect.x + width / 2 - 5, rect.y + height / 2 - 5, direction);
        if (shootSound) Mix_PlayChannel(-1, shootSound, 0);
    }

    void updateBullets(std::vector<SDL_Rect>& walls, std::vector<bool>& wallBreakable) {
        for (auto& bullet : bullets) bullet.update(walls, wallBreakable);
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](Bullet& b) { return !b.active; }), bullets.end());
    }

    void renderHealthBar(bool isPlayer1 = true) {
        SDL_Rect healthBarBg = {rect.x, rect.y - 10, width, 5};
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderFillRect(renderer, &healthBarBg);

        SDL_Rect healthBar = {rect.x, rect.y - 10, (int)(width * ((float)health / maxHealth)), 5};
        if (isPlayer1) {
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        }
        SDL_RenderFillRect(renderer, &healthBar);
    }

    void render(bool isPlayer1 = true) {
        if (!alive || !tankTexture) return;

        if (invincible && (SDL_GetTicks() / 100) % 2 == 0) {
            SDL_SetTextureAlphaMod(tankTexture, 128);
        } else {
            SDL_SetTextureAlphaMod(tankTexture, 255);
        }

        double angle;
        switch (direction) {
            case 0: angle = 0; break;
            case 1: angle = 270; break;
            case 2: angle = 180; break;
            case 3: angle = 90; break;
            default: angle = 0; break;
        }

        SDL_RenderCopyEx(renderer, tankTexture, nullptr, &rect, angle, nullptr, SDL_FLIP_NONE);
        renderHealthBar(isPlayer1);
        for (auto& bullet : bullets) bullet.render();
    }
};

class EnemyTank {
public:
    SDL_Rect rect;
    SDL_Renderer* renderer;
    std::vector<Bullet> bullets;
    bool alive;
    int direction;
    int moveTimer;
    int moveDuration;
    int moveSpeed;
    PlayerTank* target;
    int shootCooldown;
    bool frozen;
    Uint32 freezeEndTime;
    SDL_Texture* tankTexture;
    Mix_Chunk* shootSound;
    Mix_Chunk* explosionSound;

    EnemyTank(SDL_Renderer* rend, int x, int y, PlayerTank* player, Mix_Chunk* shootSnd, Mix_Chunk* explodeSnd) :
        renderer(rend), alive(true), frozen(false), freezeEndTime(0), tankTexture(nullptr),
        shootSound(shootSnd), explosionSound(explodeSnd) {
        rect = {x, y, GRID_SIZE, GRID_SIZE};
        direction = rand() % 4;
        moveTimer = 0;
        moveDuration = 50;
        moveSpeed = 2;
        target = player;
        shootCooldown = 0;

        SDL_Surface* surface = IMG_Load("tankenemy.png");
        if (!surface) {
            std::cerr << "Failed to load enemy tank texture: " << IMG_GetError() << std::endl;
        } else {
            tankTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    ~EnemyTank() {
        if (tankTexture) SDL_DestroyTexture(tankTexture);
    }

    void freeze(Uint32 duration) {
        frozen = true;
        freezeEndTime = SDL_GetTicks() + duration;
    }

    void updateFreeze() {
        if (frozen && SDL_GetTicks() > freezeEndTime) {
            frozen = false;
        }
    }

    bool checkCollision(int newX, int newY, const std::vector<SDL_Rect>& walls) {
        SDL_Rect newPos = {newX, newY, GRID_SIZE, GRID_SIZE};
        for (const auto& wall : walls) {
            if (SDL_HasIntersection(&newPos, &wall)) return true;
        }
        return false;
    }

    void update(std::vector<SDL_Rect>& walls, std::vector<bool>& wallBreakable) {
        if (!alive || frozen) return;

    // Kiểm tra nếu target không tồn tại hoặc đã chết
    if (!target || !target->alive) {
        // Nếu không có target, di chuyển ngẫu nhiên
        moveTimer++;
        if (moveTimer >= moveDuration) {
            direction = rand() % 4; // Chọn hướng ngẫu nhiên
            moveTimer = 0;
        }
        move(direction, walls);
        return;
    }

        moveTimer++;
        if (moveTimer >= moveDuration) {
            chooseDirectionTowardsPlayer();
            moveTimer = 0;
        }

        move(direction, walls);

        if (shootCooldown > 0) shootCooldown--;

        int distanceX = abs(rect.x - target->rect.x);
        int distanceY = abs(rect.y - target->rect.y);
        int shootThreshold = 200;
        if (distanceX + distanceY < shootThreshold && rand() % 100 < 10 && shootCooldown == 0) {
            shoot();
            shootCooldown = 60;
        }

        for (auto& bullet : bullets) bullet.update(walls, wallBreakable);
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](Bullet& b) { return !b.active; }), bullets.end());
    }

    void chooseDirectionTowardsPlayer() {
        if (!target) return;

        int deltaX = rect.x - target->rect.x;
        int deltaY = rect.y - target->rect.y;

        if (abs(deltaX) > abs(deltaY)) {
            direction = deltaX > 0 ? 1 : 3;
        } else {
            direction = deltaY > 0 ? 0 : 2;
        }

        if (rand() % 100 < 20) direction = rand() % 4;
    }

    void move(int dir, const std::vector<SDL_Rect>& walls) {
        if (frozen) return;

        direction = dir;
        int newX = rect.x + (dir == 1 ? -moveSpeed : dir == 3 ? moveSpeed : 0);
        int newY = rect.y + (dir == 0 ? -moveSpeed : dir == 2 ? moveSpeed : 0);

        if (newX >= 0 && newX + GRID_SIZE <= SCREEN_WIDTH && newY >= 0 && newY + GRID_SIZE <= SCREEN_HEIGHT) {
            if (!checkCollision(newX, newY, walls)) {
                rect.x = newX;
                rect.y = newY;
            } else {
                chooseDirectionTowardsPlayer();
            }
        } else {
            chooseDirectionTowardsPlayer();
        }
    }

    void shoot() {
        if (frozen) return;
        bullets.emplace_back(renderer, rect.x + GRID_SIZE / 2 - 5, rect.y + GRID_SIZE / 2 - 5, direction);
        if (shootSound) Mix_PlayChannel(-1, shootSound, 0);
    }

    void render() {
        if (!alive || !tankTexture) return;

        if (frozen) {
            SDL_SetTextureAlphaMod(tankTexture, 128);
        } else {
            SDL_SetTextureAlphaMod(tankTexture, 255);
        }

        double angle;
        switch (direction) {
            case 0: angle = 0; break;
            case 1: angle = 270; break;
            case 2: angle = 180; break;
            case 3: angle = 90; break;
            default: angle = 0; break;
        }

        SDL_RenderCopyEx(renderer, tankTexture, nullptr, &rect, angle, nullptr, SDL_FLIP_NONE);
        for (auto& bullet : bullets) bullet.render();
    }
};

class Game {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool running;
    std::vector<SDL_Rect> walls;
    std::vector<bool> wallBreakable;
    int map[MAP_ROWS][MAP_COLS];
    PlayerTank* player1;
    PlayerTank* player2;
    std::vector<EnemyTank*> enemies;
    GameState state;
    PowerUp powerUp;
    Uint32 lastPowerUpSpawnTime;
    const Uint32 powerUpSpawnInterval = 20000;

    SDL_Rect onePlayerButton;
    SDL_Rect twoPlayersButton;
    SDL_Rect restartButton;

    SDL_Texture* menuBackground;
    TTF_Font* font;
    SDL_Texture* onePlayerText;
    SDL_Texture* twoPlayersText;
    SDL_Texture* gameOverText;
    SDL_Texture* scoreText;
    SDL_Texture* restartText;
    SDL_Rect onePlayerTextRect;
    SDL_Rect twoPlayersTextRect;
    SDL_Rect gameOverTextRect;
    SDL_Rect scoreTextRect;
    SDL_Rect restartTextRect;

    Mix_Music* backgroundMusic;
    Mix_Chunk* shootSound;
    Mix_Chunk* explosionSound;
    Mix_Chunk* powerUpSound;

    SDL_Texture* buttonTexture;
    SDL_Texture* brickWallTexture;
    SDL_Texture* stoneWallTexture;
    SDL_Texture* powerUpTexture;

    int score;
    int waveNumber;
    const int baseEnemyCount = 1;
    const int scorePerEnemy = 100;
    const int waveBonus = 500;

public:
    Game() : window(nullptr), renderer(nullptr), running(true), player1(nullptr), player2(nullptr),
             state(STATE_MENU), lastPowerUpSpawnTime(0), menuBackground(nullptr), font(nullptr),
             onePlayerText(nullptr), twoPlayersText(nullptr), gameOverText(nullptr),
             scoreText(nullptr), restartText(nullptr), backgroundMusic(nullptr),
             shootSound(nullptr), explosionSound(nullptr), powerUpSound(nullptr),
             buttonTexture(nullptr), brickWallTexture(nullptr),
             stoneWallTexture(nullptr), powerUpTexture(nullptr), score(0), waveNumber(1) {
        SDL_Init(SDL_INIT_VIDEO);
        IMG_Init(IMG_INIT_PNG);
        TTF_Init();
        Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

        window = SDL_CreateWindow("Battle City", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

        onePlayerButton = {300, 200, 200, 50};
        twoPlayersButton = {300, 300, 200, 50};
        restartButton = {300, 400, 200, 50};

        loadMenuResources();
        loadMusic();
        loadSounds();
        loadPowerUpTexture();
        powerUp.texture = powerUpTexture;

        srand(time(0));
    }

    ~Game() {
        freeMenuResources();
        freeSounds();
        if (player1) delete player1;
        if (player2) delete player2;
        for (auto enemy : enemies) delete enemy;
        if (backgroundMusic) Mix_FreeMusic(backgroundMusic);
        if (buttonTexture) SDL_DestroyTexture(buttonTexture);
        if (brickWallTexture) SDL_DestroyTexture(brickWallTexture);
        if (stoneWallTexture) SDL_DestroyTexture(stoneWallTexture);
        if (powerUpTexture) SDL_DestroyTexture(powerUpTexture);
        Mix_CloseAudio();
        Mix_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }

    void loadMenuResources() {
        SDL_Surface* bgSurface = IMG_Load("nenmenu.jpg");
        if (!bgSurface) {
            std::cerr << "Failed to load menu background: " << IMG_GetError() << std::endl;
            return;
        }
        menuBackground = SDL_CreateTextureFromSurface(renderer, bgSurface);
        SDL_FreeSurface(bgSurface);

        font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 24);
        if (!font) {
            std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
            return;
        }

        SDL_Color white = {255, 255, 255, 255};
        onePlayerText = createTextTexture("1 Player", white);
        twoPlayersText = createTextTexture("2 Players", white);
        gameOverText = createTextTexture("GAME OVER", white);
        restartText = createTextTexture("Restart", white);

        onePlayerTextRect = {onePlayerButton.x + 50, onePlayerButton.y + 10, 100, 30};
        twoPlayersTextRect = {twoPlayersButton.x + 50, twoPlayersButton.y + 10, 100, 30};
        gameOverTextRect = {SCREEN_WIDTH/2 - 100, 200, 200, 50};
        restartTextRect = {restartButton.x + 50, restartButton.y + 10, 100, 30};

        SDL_Surface* buttonSurface = IMG_Load("khungmenu.jpg");
        if (!buttonSurface) {
            std::cerr << "Failed to load button texture: " << IMG_GetError() << std::endl;
            return;
        }
        buttonTexture = SDL_CreateTextureFromSurface(renderer, buttonSurface);
        SDL_FreeSurface(buttonSurface);

        SDL_Surface* brickSurface = IMG_Load("wall.png");
        if (!brickSurface) {
            std::cerr << "Failed to load brick wall texture: " << IMG_GetError() << std::endl;
        } else {
            brickWallTexture = SDL_CreateTextureFromSurface(renderer, brickSurface);
            SDL_FreeSurface(brickSurface);
        }

        SDL_Surface* stoneSurface = IMG_Load("wall.png");
        if (!stoneSurface) {
            std::cerr << "Failed to load stone wall texture: " << IMG_GetError() << std::endl;
        } else {
            stoneWallTexture = SDL_CreateTextureFromSurface(renderer, stoneSurface);
            SDL_FreeSurface(stoneSurface);
        }
    }

    void loadSounds() {
        shootSound = Mix_LoadWAV("shoot.mp3");
        if (!shootSound) {
            std::cerr << "Failed to load shoot sound: " << Mix_GetError() << std::endl;
        } else {
            Mix_VolumeChunk(shootSound, MIX_MAX_VOLUME / 8);
        }

        explosionSound = Mix_LoadWAV("explosion.mp3");
        if (!explosionSound) {
            std::cerr << "Failed to load explosion sound: " << Mix_GetError() << std::endl;
        } else {
            Mix_VolumeChunk(explosionSound, MIX_MAX_VOLUME / 4);
        }

        powerUpSound = Mix_LoadWAV("powerup.mp3");
        if (!powerUpSound) {
            std::cerr << "Failed to load powerup sound: " << Mix_GetError() << std::endl;
        } else {
            Mix_VolumeChunk(powerUpSound, MIX_MAX_VOLUME / 2);
        }
    }

    void loadPowerUpTexture() {
        SDL_Surface* surface = IMG_Load("powerup.png");
        if (!surface) {
            std::cerr << "Failed to load powerup texture: " << IMG_GetError() << std::endl;
        } else {
            powerUpTexture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    void freeMenuResources() {
        if (menuBackground) SDL_DestroyTexture(menuBackground);
        if (onePlayerText) SDL_DestroyTexture(onePlayerText);
        if (twoPlayersText) SDL_DestroyTexture(twoPlayersText);
        if (gameOverText) SDL_DestroyTexture(gameOverText);
        if (scoreText) SDL_DestroyTexture(scoreText);
        if (restartText) SDL_DestroyTexture(restartText);
        if (buttonTexture) SDL_DestroyTexture(buttonTexture);
        if (brickWallTexture) SDL_DestroyTexture(brickWallTexture);
        if (stoneWallTexture) SDL_DestroyTexture(stoneWallTexture);
        if (font) TTF_CloseFont(font);
    }

    void freeSounds() {
        if (shootSound) Mix_FreeChunk(shootSound);
        if (explosionSound) Mix_FreeChunk(explosionSound);
        if (powerUpSound) Mix_FreeChunk(powerUpSound);
    }

    void loadMusic() {
        backgroundMusic = Mix_LoadMUS("nhacnen.mp3");
        if (!backgroundMusic) {
            std::cerr << "Failed to load background music: " << Mix_GetError() << std::endl;
        } else {
            Mix_VolumeMusic(MIX_MAX_VOLUME / 2);
            Mix_PlayMusic(backgroundMusic, -1);
        }
    }

    SDL_Texture* createTextTexture(const char* text, SDL_Color color) {
        SDL_Surface* surface = TTF_RenderText_Solid(font, text, color);
        if (!surface) {
            std::cerr << "Failed to create text surface: " << TTF_GetError() << std::endl;
            return nullptr;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        return texture;
    }

    void generateMap() {
        for (int row = 0; row < MAP_ROWS; ++row) {
            for (int col = 0; col < MAP_COLS; ++col) {
                map[row][col] = 0;
            }
        }

        for (int col = 0; col < MAP_COLS; ++col) {
            map[0][col] = 1;
            map[MAP_ROWS - 1][col] = 1;
        }
        for (int row = 0; row < MAP_ROWS; ++row) {
            map[row][0] = 1;
            map[row][MAP_COLS - 1] = 1;
        }

        map[5][4] = 2;
        map[5][3] = 2;
        map[5][7] = 2;
        map[8][10] = 2;
        map[8][11] = 2;
        map[8][12] = 2;
        map[3][15] = 2;
        map[4][15] = 2;
        map[5][15] = 2;
        map[13][9] = 2;

        for (int row = 0; row < MAP_ROWS; ++row) {
            for (int col = 0; col < MAP_COLS; ++col) {
                if (map[row][col] == 1 || map[row][col] == 2) {
                    SDL_Rect wall = {col * GRID_SIZE, row * GRID_SIZE, GRID_SIZE, GRID_SIZE};
                    walls.push_back(wall);
                    wallBreakable.push_back(map[row][col] == 2);
                }
            }
        }
    }
    bool isValidSpawn(int x, int y) {
    SDL_Rect rect = {x, y, GRID_SIZE, GRID_SIZE};

    // Kiểm tra va chạm với tường
    for (const auto& wall : walls) {
        if (SDL_HasIntersection(&rect, &wall)) return false;
    }

    // Kiểm tra va chạm với người chơi
    if (player1 && SDL_HasIntersection(&rect, &player1->rect)) return false;
    if (player2 && SDL_HasIntersection(&rect, &player2->rect)) return false;

    return true;
}
void generateEnemies() {
    // Xóa quân địch cũ
    for (auto enemy : enemies) delete enemy;
    enemies.clear();

    // Số lượng quân địch tăng theo wave (ví dụ: wave 1 = 2 quân, wave 2 = 3 quân,...)
    int enemiesToSpawn = std::min(10, 1 + (waveNumber / 2));

    // Spawn quân địch ngẫu nhiên trên map
    for (int i = 0; i < enemiesToSpawn; i++) {
        int x, y;
        bool validSpawn = false;

        // Thử tìm vị trí spawn hợp lệ trong tối đa 100 lần
        for (int attempt = 0; attempt < 100; attempt++) {
            x = (rand() % (MAP_COLS - 2)) * GRID_SIZE + GRID_SIZE;  // Không spawn ở biên
            y = (rand() % (MAP_ROWS - 2)) * GRID_SIZE + GRID_SIZE;

            if (isValidSpawn(x, y)) {
                validSpawn = true;
                break;
            }
        }

        if (validSpawn) {
            PlayerTank* target = (rand() % 2 == 0 || !player2) ? player1 : player2;
            enemies.push_back(new EnemyTank(renderer, x, y, target, shootSound, explosionSound));
        }
    }
}

    void checkWaveCompletion() {
        if (enemies.empty()) {
            waveNumber++;
            score += waveBonus;
            generateEnemies();
        }
    }

    void resetGame() {
        walls.clear();
        wallBreakable.clear();
        for (auto enemy : enemies) delete enemy;
        enemies.clear();
        if (player1) delete player1;
        if (player2) delete player2;

        score = 0;
        waveNumber = 1;
        generateMap();

        int player1X = GRID_SIZE;
        int player1Y = SCREEN_HEIGHT - GRID_SIZE * 2;

        SDL_Rect playerRect = {player1X, player1Y, GRID_SIZE, GRID_SIZE};
        bool validPos = true;
        for (const auto& wall : walls) {
            if (SDL_HasIntersection(&playerRect, &wall)) {
                validPos = false;
                break;
            }
        }

        if (!validPos) {
            player1X = GRID_SIZE * 2;
            player1Y = SCREEN_HEIGHT - GRID_SIZE * 3;
        }

        player1 = new PlayerTank(renderer, player1X, player1Y, shootSound);

        if (state == STATE_2P) {
            int player2X = SCREEN_WIDTH - GRID_SIZE * 2;
            int player2Y = SCREEN_HEIGHT - GRID_SIZE * 2;

            playerRect = {player2X, player2Y, GRID_SIZE, GRID_SIZE};
            validPos = true;
            for (const auto& wall : walls) {
                if (SDL_HasIntersection(&playerRect, &wall)) {
                    validPos = false;
                    break;
                }
            }

            if (!validPos) {
                player2X = SCREEN_WIDTH - GRID_SIZE * 3;
                player2Y = SCREEN_HEIGHT - GRID_SIZE * 3;
            }

            player2 = new PlayerTank(renderer, player2X, player2Y, shootSound);
        }

        generateEnemies();
        powerUp.active = false;
        lastPowerUpSpawnTime = SDL_GetTicks();
    }

    PowerUpType getRandomPowerUpType() {
        int random = rand() % 100;
        if (random < 30) return POWERUP_HEALTH;
        else if (random < 60) return POWERUP_FREEZE;
        else if (random < 90) return POWERUP_INVINCIBLE;
        else return POWERUP_BOMB;
    }

    void spawnRandomPowerUp() {
        if (powerUp.active) return;

        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - lastPowerUpSpawnTime > powerUpSpawnInterval) {
            int x = (rand() % (MAP_COLS - 2)) * GRID_SIZE + GRID_SIZE;
            int y = (rand() % (MAP_ROWS - 2)) * GRID_SIZE + GRID_SIZE;

            bool validPosition = true;
            SDL_Rect powerUpRect = {x, y, GRID_SIZE, GRID_SIZE};
            for (const auto& wall : walls) {
                if (SDL_HasIntersection(&powerUpRect, &wall)) {
                    validPosition = false;
                    break;
                }
            }

            if (validPosition) {
                PowerUpType type = getRandomPowerUpType();
                powerUp.spawn(x, y, type);
                lastPowerUpSpawnTime = currentTime;
            }
        }
    }

    void checkPowerUpCollision() {
        if (!powerUp.active) return;

        if (player1 && player1->alive && player1->checkPowerUpCollision(powerUp.rect)) {
            applyPowerUpEffect(player1);
            powerUp.active = false;
        }
        else if (player2 && player2->alive && player2->checkPowerUpCollision(powerUp.rect)) {
            applyPowerUpEffect(player2);
            powerUp.active = false;
        }
    }

    void applyPowerUpEffect(PlayerTank* player) {
        if (powerUpSound) Mix_PlayChannel(-1, powerUpSound, 0);
        switch (powerUp.type) {
            case POWERUP_HEALTH: player->heal(); break;
            case POWERUP_FREEZE: freezeAllEnemies(5000); break;
            case POWERUP_INVINCIBLE: player->activateInvincible(5000); break;
            case POWERUP_BOMB: destroyAllEnemies(); break;
            default: break;
        }
    }

    void freezeAllEnemies(Uint32 duration) {
        for (auto enemy : enemies) {
            enemy->freeze(duration);
        }
    }

    void destroyAllEnemies() {
        for (auto enemy : enemies) {
            enemy->alive = false;
            score += scorePerEnemy;
            if (explosionSound) Mix_PlayChannel(-1, explosionSound, 0);
        }
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](EnemyTank* e) {
            if (!e->alive) { delete e; return true; }
            return false;
        }), enemies.end());
        checkWaveCompletion();
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            switch (state) {
                case STATE_MENU:
                    Mix_PauseMusic();
                    if (event.type == SDL_MOUSEBUTTONDOWN) {
                        int x = event.button.x;
                        int y = event.button.y;

                        if (x >= onePlayerButton.x && x <= onePlayerButton.x + onePlayerButton.w &&
                            y >= onePlayerButton.y && y <= onePlayerButton.y + onePlayerButton.h) {
                            state = STATE_1P;
                            resetGame();
                            Mix_ResumeMusic();
                        }
                        else if (x >= twoPlayersButton.x && x <= twoPlayersButton.x + twoPlayersButton.w &&
                                 y >= twoPlayersButton.y && y <= twoPlayersButton.y + twoPlayersButton.h) {
                            state = STATE_2P;
                            resetGame();
                            Mix_ResumeMusic();
                        }
                    }
                    break;

                case STATE_GAME_OVER:
                    if (event.type == SDL_MOUSEBUTTONDOWN) {
                        int x = event.button.x;
                        int y = event.button.y;
                        if (x >= restartButton.x && x <= restartButton.x + restartButton.w &&
                            y >= restartButton.y && y <= restartButton.y + restartButton.h) {
                            state = STATE_MENU;
                        }
                    }
                    break;

                case STATE_1P:
                case STATE_2P:
                    if (player1) player1->handleInput(event, true);
                    if (player2) player2->handleInput(event, false);
                    break;
            }
        }
    }

    void update() {
        if (state == STATE_1P || state == STATE_2P) {
            if (player1) {
                player1->update(walls, player2 ? &player2->rect : nullptr);
                player1->updateBullets(walls, wallBreakable);
            }
            if (player2) {
                player2->update(walls, &player1->rect);
                player2->updateBullets(walls, wallBreakable);
            }

            for (auto enemy : enemies) {
                enemy->update(walls, wallBreakable);
                enemy->updateFreeze();
                if (player1) {
                    for (auto& bullet : enemy->bullets) {
                        if (SDL_HasIntersection(&bullet.rect, &player1->rect)) {
                            if (!player1->invincible) {
                                player1->takeDamage();
                                bullet.active = false;
                                player1->activateInvincible(1000);
                                if (explosionSound) Mix_PlayChannel(-1, explosionSound, 0);
                            }
                        }
                    }
                }
                if (player2) {
                    for (auto& bullet : enemy->bullets) {
                        if (SDL_HasIntersection(&bullet.rect, &player2->rect)) {
                            if (!player2->invincible) {
                                player2->takeDamage();
                                bullet.active = false;
                                player2->activateInvincible(1000);
                                if (explosionSound) Mix_PlayChannel(-1, explosionSound, 0);
                            }
                        }
                    }
                }

                if (player1) {
                    for (auto& bullet : player1->bullets) {
                        if (SDL_HasIntersection(&bullet.rect, &enemy->rect)) {
                            enemy->alive = false;
                            bullet.active = false;
                            score += scorePerEnemy;
                            if (explosionSound) Mix_PlayChannel(-1, explosionSound, 0);
                        }
                    }
                }
                if (player2) {
                    for (auto& bullet : player2->bullets) {
                        if (SDL_HasIntersection(&bullet.rect, &enemy->rect)) {
                            enemy->alive = false;
                            bullet.active = false;
                            score += scorePerEnemy;
                            if (explosionSound) Mix_PlayChannel(-1, explosionSound, 0);
                        }
                    }
                }
            }

            enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](EnemyTank* e) {
                if (!e->alive) { delete e; return true; }
                return false;
            }), enemies.end());

            checkWaveCompletion();
            spawnRandomPowerUp();
            powerUp.update();
            checkPowerUpCollision();

            bool gameOver = false;
            if (state == STATE_1P && (!player1 || !player1->alive)) {
                gameOver = true;
            }
            else if (state == STATE_2P ) {
               bool player1Dead = (!player1 || !player1->alive);
        bool player2Dead = (!player2 || !player2->alive);
        gameOver = (player1Dead && player2Dead);
            }

            if (gameOver) {
                state = STATE_GAME_OVER;
                SDL_Color white = {255, 255, 255, 255};
                char scoreStr[50];
                sprintf(scoreStr, "Final Score: %d", score);
                if (scoreText) SDL_DestroyTexture(scoreText);
                scoreText = createTextTexture(scoreStr, white);
                scoreTextRect = {SCREEN_WIDTH/2 - 100, 300, 200, 30};
            }
        }
    }

    void render() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        switch (state) {
            case STATE_MENU:
                if (menuBackground) {
                    SDL_RenderCopy(renderer, menuBackground, nullptr, nullptr);
                }
                if (buttonTexture) {
                    SDL_RenderCopy(renderer, buttonTexture, nullptr, &onePlayerButton);
                    SDL_RenderCopy(renderer, buttonTexture, nullptr, &twoPlayersButton);
                }
                if (onePlayerText) SDL_RenderCopy(renderer, onePlayerText, nullptr, &onePlayerTextRect);
                if (twoPlayersText) SDL_RenderCopy(renderer, twoPlayersText, nullptr, &twoPlayersTextRect);
                break;

            case STATE_GAME_OVER:
                if (gameOverText) SDL_RenderCopy(renderer, gameOverText, nullptr, &gameOverTextRect);
                if (scoreText) SDL_RenderCopy(renderer, scoreText, nullptr, &scoreTextRect);
                if (buttonTexture) SDL_RenderCopy(renderer, buttonTexture, nullptr, &restartButton);
                if (restartText) SDL_RenderCopy(renderer, restartText, nullptr, &restartTextRect);
                break;

            case STATE_1P:
            case STATE_2P:
                for (size_t i = 0; i < walls.size(); ++i) {
                    if (wallBreakable[i] && brickWallTexture) {
                        SDL_RenderCopy(renderer, brickWallTexture, nullptr, &walls[i]);
                    } else if (stoneWallTexture) {
                        SDL_RenderCopy(renderer, stoneWallTexture, nullptr, &walls[i]);
                    }
                }
                if (player1) player1->render(true);
                if (player2) player2->render(false);
                for (auto enemy : enemies) enemy->render();
                powerUp.render(renderer);

                // Hiển thị điểm và wave
                SDL_Color white = {255, 255, 255, 255};
                char scoreStr[50];
                sprintf(scoreStr, "Score: %d", score);
                SDL_Texture* scoreTexture = createTextTexture(scoreStr, white);
                SDL_Rect scoreRect = {10, 10, 150, 30};
                SDL_RenderCopy(renderer, scoreTexture, nullptr, &scoreRect);
                SDL_DestroyTexture(scoreTexture);

                char waveStr[50];
                sprintf(waveStr, "Wave: %d", waveNumber);
                SDL_Texture* waveTexture = createTextTexture(waveStr, white);
                SDL_Rect waveRect = {10, 50, 150, 30};
                SDL_RenderCopy(renderer, waveTexture, nullptr, &waveRect);
                SDL_DestroyTexture(waveTexture);
                break;
        }

        SDL_RenderPresent(renderer);
    }

    void run() {
        Uint32 frameStart;
        int frameTime;
        const int FPS = 60;
        const int frameDelay = 1000 / FPS;

        while (running) {
            frameStart = SDL_GetTicks();

            handleEvents();
            update();
            render();

            frameTime = SDL_GetTicks() - frameStart;
            if (frameDelay > frameTime) {
                SDL_Delay(frameDelay - frameTime);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    Game game;
    game.run();
    return 0;
}
