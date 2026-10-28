#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "cube.h"

// 全局状态
uint8_t cube[54];
int active_move = -1;       // 当前正在执行的转动
float anim_progress = 0.0f; // 动画进度 0.0 -> 1.0

// 相机控制
float camera_angle_x = 0.785f; // 45度
float camera_angle_y = 0.615f; // 约35度
float camera_distance = 8.0f;

// 六个面的外法向，顺序 U R F D L B（与 cube.h 一致）
static const int FACE_N[6][3] = {
    { 0,  1,  0}, { 1,  0,  0}, { 0,  0,  1},
    { 0, -1,  0}, {-1,  0,  0}, { 0,  0, -1},
};

void trigger_move(int move_id) {
    if (active_move != -1) return;
    active_move = move_id;
    anim_progress = 0.0f;
}

int main(void) {
    build_perm_tables();
    cube_init(cube);

    InitWindow(900, 700, "3D Rubik's Cube - Original");
    SetTargetFPS(60);

    Camera3D camera = { 0 };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Color colors[6] = { WHITE, RED, GREEN, YELLOW, ORANGE, BLUE };

    while (!WindowShouldClose()) {
        // ---- 1. 鼠标拖拽视角 ----
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 delta = GetMouseDelta();
            camera_angle_x -= delta.x * 0.01f;
            camera_angle_y += delta.y * 0.01f;
            if (camera_angle_y > 1.5f) camera_angle_y = 1.5f;
            if (camera_angle_y < -1.5f) camera_angle_y = -1.5f;
        }

        camera.position.x = camera.target.x + camera_distance * cosf(camera_angle_y) * sinf(camera_angle_x);
        camera.position.y = camera.target.y + camera_distance * sinf(camera_angle_y);
        camera.position.z = camera.target.z + camera_distance * cosf(camera_angle_y) * cosf(camera_angle_x);

        // ---- 2. 键盘输入转动 ----
        // 修复：先判断 Shift，再一次性决定是顺时针(+0)还是逆时针(+2)。
        // 原来先无条件触发顺时针，active_move 被占用，后面的逆时针永远被忽略。
        if (active_move == -1) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            int v = shift ? 2 : 0; // 编号布局: base+0 顺时针, base+1 180°, base+2 逆时针

            if      (IsKeyPressed(KEY_U)) trigger_move(MOVE_U + v);
            else if (IsKeyPressed(KEY_R)) trigger_move(MOVE_R + v);
            else if (IsKeyPressed(KEY_F)) trigger_move(MOVE_F + v);
            else if (IsKeyPressed(KEY_D)) trigger_move(MOVE_D + v);
            else if (IsKeyPressed(KEY_L)) trigger_move(MOVE_L + v);
            else if (IsKeyPressed(KEY_B)) trigger_move(MOVE_B + v);
        }

        // ---- 3. 动画逻辑 ----
        if (active_move != -1) {
            anim_progress += GetFrameTime() * 8.0f;
            if (anim_progress >= 1.0f) {
                apply_move(cube, active_move);
                active_move = -1;
                anim_progress = 0.0f;
            }
        }

        // ---- 4. 绘制 ----
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);

        DrawCube((Vector3){0,0,0}, 2.9f, 2.9f, 2.9f, BLACK);

        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++) {
                    if (x == 0 && y == 0 && z == 0) continue;

                    Vector3 pos = { (float)x, (float)y, (float)z };

                    // 判断当前 cubie 是否在动画层内，并算出旋转轴/角度
                    bool in_layer = false;
                    Vector3 axis = {0};
                    float angle_deg = 0.0f;

                    if (active_move != -1) {
                        int face = active_move / 3;
                        int variant = active_move % 3;
                        int layer_val = (face < 3) ? 1 : -1;

                        if (face == 0 || face == 3) { in_layer = (y == layer_val); axis = (Vector3){0,1,0}; }
                        if (face == 1 || face == 4) { in_layer = (x == layer_val); axis = (Vector3){1,0,0}; }
                        if (face == 2 || face == 5) { in_layer = (z == layer_val); axis = (Vector3){0,0,1}; }

                        // 修复：从面外侧看顺时针 = 绕外法向轴的右手系负方向。
                        // U/R/F(法向为正轴) -> -1；D/L/B(法向为负轴) -> +1。
                        // 原代码只有 U/L/B 取 -1，导致 R/F/L/B 方向全错，且与 cube.c 的置换表不一致。
                        float dir = (face < 3) ? -1.0f : 1.0f;
                        if (variant == 2) dir = -dir;                 // 逆时针
                        float total = (variant == 1) ? 180.0f : 90.0f; // 180° 或 90°
                        angle_deg = total * anim_progress * dir;
                    }

                    // 修复：用矩阵整体旋转该层 cubie，而不是只旋转中心点。
                    // 这样黑色底座和贴纸朝向都会正确跟着转，不再出现贴纸"平移不转向"的错位。
                    if (in_layer) {
                        rlPushMatrix();
                        rlRotatef(angle_deg, axis.x, axis.y, axis.z);
                    }

                    DrawCube(pos, 0.95f, 0.95f, 0.95f, BLACK);

                    for (int f = 0; f < 6; f++) {
                        int nx = FACE_N[f][0], ny = FACE_N[f][1], nz = FACE_N[f][2];

                        bool is_outer = (nx != 0 && x == nx) || (ny != 0 && y == ny) || (nz != 0 && z == nz);
                        if (!is_outer) continue;

                        // 与 cube.c 的 face_axes 一致的 (row, col) 映射
                        int r = 0, c = 0;
                        if (f == 0) { r = z + 1; c = x + 1; }  // U
                        if (f == 1) { r = 1 - y; c = 1 - z; }  // R
                        if (f == 2) { r = 1 - y; c = x + 1; }  // F
                        if (f == 3) { r = 1 - z; c = x + 1; }  // D
                        if (f == 4) { r = 1 - y; c = z + 1; }  // L
                        if (f == 5) { r = 1 - y; c = 1 - x; }  // B

                        uint8_t color_idx = cube[f * 9 + r * 3 + c];

                        float offset_dist = 0.485f;
                        Vector3 sticker_pos = {
                            pos.x + nx * offset_dist,
                            pos.y + ny * offset_dist,
                            pos.z + nz * offset_dist
                        };

                        DrawCube(sticker_pos,
                                 nx != 0 ? 0.05f : 0.85f,
                                 ny != 0 ? 0.05f : 0.85f,
                                 nz != 0 ? 0.05f : 0.85f,
                                 colors[color_idx]);
                    }

                    if (in_layer) rlPopMatrix();
                }
            }
        }

        EndMode3D();
        DrawText("Drag mouse to rotate view", 10, 10, 20, DARKGRAY);
        DrawText("Press U, R, F, D, L, B to turn faces", 10, 35, 20, DARKGRAY);
        DrawText("Hold SHIFT + Key for counter-clockwise", 10, 60, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}