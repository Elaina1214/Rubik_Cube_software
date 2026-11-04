#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "cube.h"
#include "solver.h"
#include <string.h>
#include <stdio.h>

/* 动画按角速度推进，而不是让 90° 和 180° 使用相同总时长。
 * 270°/秒时，90°约 0.33 秒，180°约 0.67 秒。 */
#define ANGULAR_SPEED 270.0f
/* 限制单帧参与动画计算的时间，避免窗口卡顿后进度突然跳跃。 */
#define MAX_ANIM_FRAME_TIME (1.0f / 30.0f)

uint8_t cube[54];
int active_move = -1;
float anim_progress = 0.0f;
float camera_angle_x = 0.785f;
float camera_angle_y = 0.615f;
float camera_distance = 8.0f;

int solve_queue[200]; 
int solve_len = 0;    
int solve_idx = 0;    /* 已经完整执行的步骤数 */
float solve_step_pause = 0.0f; /* 两个独立步骤之间的短暂停顿 */

#define SOLVE_QUEUE_CAPACITY ((int)(sizeof(solve_queue) / sizeof(solve_queue[0])))

/* 按 S 时如果正好在转动的动画中，先记下请求，等这一转结束再求解，
 * 避免按键被直接丢掉（外观和行为与之前一致）。 */
int solve_request = 0;

/* 手动转动的按键缓冲：动画进行中按下的键先存起来，动画结束后执行，
 * 否则动画变慢后，转动期间的按键会被丢掉，看起来像"没反应"。 */
int pending_move = -1;

/* 防御性处理：自动求解播放队列中绝不保留半圈动作（R2/L2 等）。
 * 将任意半圈动作展开为同一面的两个独立 90° 顺时针动作。
 * 即使 solver.c 返回了半圈动作，这里也会在进入动画队列前拆开。 */
static int expand_half_turns(const int *src, int src_len, int *dst, int dst_cap) {
    int out = 0;
    for (int i = 0; i < src_len; ++i) {
        int move = src[i];
        if (move < 0 || move >= 18) return -1;
        if (move % 3 == 1) {
            if (out + 2 > dst_cap) return -1;
            int quarter = (move / 3) * 3;
            dst[out++] = quarter;
            dst[out++] = quarter;
        } else {
            if (out + 1 > dst_cap) return -1;
            dst[out++] = move;
        }
    }
    return out;
}

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
    solver_init();          /* 预建求解表（约 0.1 秒），避免第一次按 S 时卡顿 */
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
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 delta = GetMouseDelta();
            camera_angle_x -= delta.x * 0.01f;
            camera_angle_y += delta.y * 0.01f;
            if (camera_angle_y > 1.5f) camera_angle_y = 1.5f;
            if (camera_angle_y < -1.5f) camera_angle_y = -1.5f;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            camera_distance -= wheel * 0.5f;
            if (camera_distance < 4.0f) camera_distance = 4.0f;
            if (camera_distance > 15.0f) camera_distance = 15.0f;
        }

        camera.position.x = camera.target.x + camera_distance * cosf(camera_angle_y) * sinf(camera_angle_x);
        camera.position.y = camera.target.y + camera_distance * sinf(camera_angle_y);
        camera.position.z = camera.target.z + camera_distance * cosf(camera_angle_y) * cosf(camera_angle_x);

        /* 空格：打断自动求解。清空队列；正在进行的这一步会正常转完，
         * 保证魔方状态始终是完整的（不会停在半转状态）。 */
        if (IsKeyPressed(KEY_SPACE)) {
            solve_request = 0;
            pending_move = -1;
            if (solve_idx < solve_len) printf("Solve interrupted at %d/%d.\n", solve_idx, solve_len);
            solve_len = 0;
            solve_idx = 0;
            solve_step_pause = 0.0f;
        }

        /* 手动转动：任何时候都读取按键存入缓冲；自动播放期间不接收 */
        if (solve_idx >= solve_len) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            int v = shift ? 2 : 0;
            if      (IsKeyPressed(KEY_U)) pending_move = MOVE_U + v;
            else if (IsKeyPressed(KEY_R)) pending_move = MOVE_R + v;
            else if (IsKeyPressed(KEY_F)) pending_move = MOVE_F + v;
            else if (IsKeyPressed(KEY_D)) pending_move = MOVE_D + v;
            else if (IsKeyPressed(KEY_L)) pending_move = MOVE_L + v;
            else if (IsKeyPressed(KEY_B)) pending_move = MOVE_B + v;

            if (active_move == -1 && pending_move != -1) {
                trigger_move(pending_move);
                pending_move = -1;
            }
        }

        if (IsKeyPressed(KEY_S)) solve_request = 1;
        if (solve_request && solve_idx < solve_len) solve_request = 0; /* 求解中忽略重复请求 */

        if (solve_request && active_move == -1) {
            solve_request = 0;
            pending_move = -1;

            uint8_t cube_copy[54];
            memcpy(cube_copy, cube, 54); 
            
            printf("\n--- Pressed S, Calculating ---\n");
            int raw_queue[SOLVE_QUEUE_CAPACITY];
            int raw_len = solve_cube(cube_copy, raw_queue, SOLVE_QUEUE_CAPACITY);
            solve_len = (raw_len >= 0)
                ? expand_half_turns(raw_queue, raw_len, solve_queue, SOLVE_QUEUE_CAPACITY)
                : -1;
            solve_idx = 0;
            solve_step_pause = 0.0f;

            if (solve_len > 0 && solve_len <= SOLVE_QUEUE_CAPACITY) {
                printf("Success! Need %d individual steps.\n", solve_len);
                trigger_move(solve_queue[solve_idx]);
            } else if (solve_len == 0) {
                printf("Cube is already solved.\n");
            } else {
                /* 求解失败或结果长度超出队列容量：不播放，避免越界读取 */
                solve_len = 0;
                printf("Failed! Solver stuck.\n");
            }
        }

        /* 每一步必须等上一转完整结束，并经过短暂停顿后才启动。 */
        if (solve_step_pause > 0.0f) {
            solve_step_pause -= GetFrameTime();
            if (solve_step_pause < 0.0f) solve_step_pause = 0.0f;
        }
        if (active_move == -1 && solve_idx < solve_len && solve_step_pause <= 0.0f) {
            trigger_move(solve_queue[solve_idx]);
        }

        /* 只推进动画进度，不在绘制前提交逻辑状态。
         * 到达 1.0 时保留 active_move，让本帧先完整画出 90°/180°终点姿态；
         * 在 EndDrawing() 后再提交状态，避免最后一帧被提前切回静止姿态。 */
        bool finish_move_after_draw = false;
        if (active_move != -1) {
            float frame_time = GetFrameTime();
            if (frame_time < 0.0f) frame_time = 0.0f;
            if (frame_time > MAX_ANIM_FRAME_TIME)
                frame_time = MAX_ANIM_FRAME_TIME;

            /* 自动求解队列已将半圈动作拆成两个独立的 90° 动作；
             * 因此每个播放步骤都只旋转 90°，不会出现半圈动画。 */
            float total_degrees = 90.0f;
            anim_progress += frame_time * ANGULAR_SPEED / total_degrees;
            if (anim_progress >= 1.0f) {
                anim_progress = 1.0f;
                finish_move_after_draw = true;
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);

        /* 中心核心只需填充魔方内部的小空间。原来的 2.9f 会在转动时
         * 从外层小方块之间露出大面积黑色平面；缩小后只保留内部机构的效果。 */
        DrawCube((Vector3){0,0,0}, 0.9f, 0.9f, 0.9f, DARKGRAY);

        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++) {
                    if (x == 0 && y == 0 && z == 0) continue;
                    Vector3 pos = { (float)x, (float)y, (float)z };
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
                        float dir = (face < 3) ? -1.0f : 1.0f;
                        if (variant == 2) dir = -dir;
                        float total = (variant == 1) ? 180.0f : 90.0f;
                        float progress = anim_progress;
                        if (progress < 0.0f) progress = 0.0f;
                        if (progress > 1.0f) progress = 1.0f;
                        angle_deg = total * progress * dir;
                    }

                    if (in_layer) {
                        rlPushMatrix();
                        rlRotatef(angle_deg, axis.x, axis.y, axis.z);
                    }

                    /* 深灰色作为小方块本体，避免贴纸边缘的细小缝隙显得纯黑。 */
                    DrawCube(pos, 0.95f, 0.95f, 0.95f, DARKGRAY);

                    for (int f = 0; f < 6; f++) {
                        int nx = FACE_N[f][0], ny = FACE_N[f][1], nz = FACE_N[f][2];
                        bool is_outer = (nx != 0 && x == nx) || (ny != 0 && y == ny) || (nz != 0 && z == nz);
                        if (!is_outer) continue;

                        int r = 0, c = 0;
                        if (f == 0) { r = z + 1; c = x + 1; }
                        if (f == 1) { r = 1 - y; c = 1 - z; }
                        if (f == 2) { r = 1 - y; c = x + 1; }
                        if (f == 3) { r = 1 - z; c = x + 1; }
                        if (f == 4) { r = 1 - y; c = z + 1; }
                        if (f == 5) { r = 1 - y; c = 1 - x; }

                        uint8_t color_idx = cube[f * 9 + r * 3 + c];
                        if (color_idx > 5) color_idx = 0; /* 防御：非法颜色索引会让 colors[] 越界读 */
                        /* 贴纸略微伸出本体表面，并覆盖大部分面；保留极细的深色边框。 */
                        float offset_dist = 0.49f;
                        Vector3 sticker_pos = {
                            pos.x + nx * offset_dist,
                            pos.y + ny * offset_dist,
                            pos.z + nz * offset_dist
                        };

                        DrawCube(sticker_pos,
                                 nx != 0 ? 0.04f : 0.92f,
                                 ny != 0 ? 0.04f : 0.92f,
                                 nz != 0 ? 0.04f : 0.92f,
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
        DrawText("Press S to Auto-Solve", 10, 85, 20, DARKGREEN);
        if (solve_len > 0) {
            int shown_step = solve_idx + ((active_move != -1 && solve_idx < solve_len) ? 1 : 0);
            const char *face_names[6] = { "U", "R", "F", "D", "L", "B" };
            char move_label[8] = "--";
            if (active_move != -1) {
                int face = active_move / 3;
                int variant = active_move % 3;
                snprintf(move_label, sizeof(move_label), "%s%s", face_names[face],
                         variant == 1 ? "2" : variant == 2 ? "'" : "");
            }
            DrawText(TextFormat("Step: %d/%d   Move: %s   (SPACE to stop)",
                                shown_step, solve_len, move_label), 10, 110, 20, MAROON);
        }
        EndDrawing();

        /* 在完整终点帧显示之后更新魔方数组。旋转动画与逻辑状态在边界处连续。 */
        if (finish_move_after_draw && active_move != -1) {
            apply_move(cube, active_move);
            active_move = -1;
            anim_progress = 0.0f;
            if (solve_len > 0 && solve_idx < solve_len) {
                solve_idx++; /* 只有完整转完，才计为已完成步骤 */
                if (solve_idx < solve_len) solve_step_pause = 0.12f;
            }
        }

        if (active_move == -1 && solve_idx >= solve_len) {
            solve_len = 0;
            solve_idx = 0;
            solve_step_pause = 0.0f;
        }
    }

    CloseWindow();
    return 0;
}