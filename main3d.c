#include "raylib.h"
#include "raymath.h"
#include "cube.h"

// 全局状态
uint8_t cube[54];
int active_move = -1;       // 当前正在执行的转动
float anim_progress = 0.0f; // 动画进度 0.0 -> 1.0

// 相机控制
float camera_angle_x = 0.785f; // 45度
float camera_angle_y = 0.615f; // 约35度
float camera_distance = 8.0f;

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
            // 限制垂直角度，防止翻转
            if (camera_angle_y > 1.5f) camera_angle_y = 1.5f;
            if (camera_angle_y < -1.5f) camera_angle_y = -1.5f;
        }

        // 计算相机位置 (球面坐标)
        camera.position.x = camera.target.x + camera_distance * cosf(camera_angle_y) * sinf(camera_angle_x);
        camera.position.y = camera.target.y + camera_distance * sinf(camera_angle_y);
        camera.position.z = camera.target.z + camera_distance * cosf(camera_angle_y) * cosf(camera_angle_x);

        // ---- 2. 键盘输入转动 ----
        if (active_move == -1) {
            if (IsKeyPressed(KEY_U)) trigger_move(MOVE_U);
            if (IsKeyPressed(KEY_R)) trigger_move(MOVE_R);
            if (IsKeyPressed(KEY_F)) trigger_move(MOVE_F);
            if (IsKeyPressed(KEY_D)) trigger_move(MOVE_D);
            if (IsKeyPressed(KEY_L)) trigger_move(MOVE_L);
            if (IsKeyPressed(KEY_B)) trigger_move(MOVE_B);
            
            // 加上 Shift 键实现逆时针
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (shift) {
                if (IsKeyPressed(KEY_U)) trigger_move(MOVE_UP);
                if (IsKeyPressed(KEY_R)) trigger_move(MOVE_RP);
                if (IsKeyPressed(KEY_F)) trigger_move(MOVE_FP);
                if (IsKeyPressed(KEY_D)) trigger_move(MOVE_DP);
                if (IsKeyPressed(KEY_L)) trigger_move(MOVE_LP);
                if (IsKeyPressed(KEY_B)) trigger_move(MOVE_BP);
            }
        }

        // ---- 3. 动画逻辑 ----
        if (active_move != -1) {
            anim_progress += GetFrameTime() * 8.0f; // 动画速度
            if (anim_progress >= 1.0f) {
                anim_progress = 0.0f;
                apply_move(cube, active_move); // 动画结束，更新底层数据
                active_move = -1;              // 释放状态
            }
        }

        // ---- 4. 绘制 ----
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);

        // 绘制轴心核心
        DrawCube((Vector3){0,0,0}, 2.9f, 2.9f, 2.9f, BLACK);

        // 遍历 27 个小立方体 (x, y, z 从 -1 到 1)
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++) {
                    if (x == 0 && y == 0 && z == 0) continue; // 跳过中心

                    Vector3 pos = { (float)x, (float)y, (float)z };
                    float angle = 0.0f;
                    Vector3 axis = {0};

                    // 判断当前 cubie 是否在动画层内
                    if (active_move != -1) {
                        int face = active_move / 3;
                        int variant = active_move % 3;
                        bool in_layer = false;
                        int layer_val = (face == 0 || face == 1 || face == 2) ? 1 : -1;

                        if (face == 0 || face == 3) { if (y == layer_val) { in_layer = true; axis = (Vector3){0,1,0}; } }
                        if (face == 1 || face == 4) { if (x == layer_val) { in_layer = true; axis = (Vector3){1,0,0}; } }
                        if (face == 2 || face == 5) { if (z == layer_val) { in_layer = true; axis = (Vector3){0,0,1}; } }

                        if (in_layer) {
                            float target_angle = 90.0f * DEG2RAD;
                            float dir = 1.0f;
                            if (face == 0 || face == 4 || face == 5) dir = -1.0f; // 根据右手定则修正方向
                            if (variant == 2) dir = -dir; // 逆时针
                            if (variant == 1) target_angle *= 2.0f; // 180度

                            angle = target_angle * anim_progress * dir;
                            
                            // 动画期间实际绘制的旋转位置
                            pos = Vector3RotateByAxisAngle(pos, axis, angle);
                        }
                    }

                    // 绘制黑色底座
                    DrawCube(pos, 0.95f, 0.95f, 0.95f, BLACK);

                    // 绘制贴纸
                    for (int f = 0; f < 6; f++) {
                        int nx = 0, ny = 0, nz = 0;
                        if (f == 0) ny = 1; if (f == 1) nx = 1; if (f == 2) nz = 1;
                        if (f == 3) ny = -1; if (f == 4) nx = -1; if (f == 5) nz = -1;

                        // 检查该面是否朝外
                        bool is_outer = (nx != 0 && x == nx) || (ny != 0 && y == ny) || (nz != 0 && z == nz);
                        if (!is_outer) continue;

                        // 根据坐标映射数组索引
                        int r = 0, c = 0;
                        if (f == 0) { r = z + 1; c = x + 1; }       // U (从上往下看)
                        if (f == 1) { r = 1 - y; c = 1 - z; }       // R (从右往左看)
                        if (f == 2) { r = 1 - y; c = x + 1; }       // F (从前往后看)
                        if (f == 3) { r = 1 - z; c = x + 1; }       // D (从下往上看)
                        if (f == 4) { r = 1 - y; c = z + 1; }       // L (从左往右看)
                        if (f == 5) { r = 1 - y; c = 1 - x; }       // B (从后往前看)

                        int idx = f * 9 + r * 3 + c;
                        uint8_t color_idx = cube[idx];

                        // 计算贴纸位置
                        float offset_dist = 0.485f;
                        Vector3 offset = { nx * offset_dist, ny * offset_dist, nz * offset_dist };
                        Vector3 sticker_pos = Vector3Add(pos, offset);
                        
                        // 如果是动画层，贴纸也要跟着转
                        if (active_move != -1 && angle != 0.0f) {
                             sticker_pos = Vector3RotateByAxisAngle(sticker_pos, axis, angle);
                        }

                        DrawCube(sticker_pos, 
                                 nx != 0 ? 0.05f : 0.85f, 
                                 ny != 0 ? 0.05f : 0.85f, 
                                 nz != 0 ? 0.05f : 0.85f, 
                                 colors[color_idx]);
                    }
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