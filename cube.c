#include "cube.h"
#include <string.h>
#include <stdio.h>

uint8_t perm[18][54];

/* ============================================================
 * 几何生成法：为 54 个格子建立 3D 描述，通过绕轴旋转 90°
 * 自动推导出 6 张“顺时针”基础置换表，避免手写 324 个数字。
 * ============================================================ */

typedef struct {
    int x, y, z;      // 小立方体中心坐标，取值 -1/0/1
    int nx, ny, nz;   // 该色块所在面的外法向（单位向量）
} Sticker;

/* 六个面的法向，顺序 U R F D L B */
static const int FACE_NORMAL[6][3] = {
    { 0,  1,  0}, // U
    { 1,  0,  0}, // R
    { 0,  0,  1}, // F
    { 0, -1,  0}, // D
    {-1,  0,  0}, // L
    { 0,  0, -1}, // B
};

/* 为每个面定义面内两个切向轴：
 *   u = 面对该面看时的“向右”方向
 *   v = 面对该面看时的“向下”方向
 * 满足 u × v = 外法向，从而 (row, col) 行优先对应从左上到右下。
 */
static void face_axes(int f, int *ux, int *uy, int *uz, int *vx, int *vy, int *vz) {
    switch (f) {
        case 0: /* U：从上往下看，右=+x，下=+z */
            *ux=1; *uy=0; *uz=0;  *vx=0; *vy=0; *vz=1; break;
        case 1: /* R：从右往左看，右=-z，下=-y */
            *ux=0; *uy=0; *uz=-1; *vx=0; *vy=-1; *vz=0; break;
        case 2: /* F：从前往后看，右=+x，下=-y */
            *ux=1; *uy=0; *uz=0;  *vx=0; *vy=-1; *vz=0; break;
        case 3: /* D：从下往上看，右=+x，下=-z */
            *ux=1; *uy=0; *uz=0;  *vx=0; *vy=0; *vz=-1; break;
        case 4: /* L：从左往右看，右=+z，下=-y */
            *ux=0; *uy=0; *uz=1;  *vx=0; *vy=-1; *vz=0; break;
        default: /* B：从后往前看，右=-x，下=-y */
            *ux=-1; *uy=0; *uz=0; *vx=0; *vy=-1; *vz=0; break;
    }
}

/* 生成 54 个格子的 3D 描述，顺序与 cube[54] 完全一致 */
static void build_stickers(Sticker s[54]) {
    for (int f = 0; f < 6; f++) {
        int nx = FACE_NORMAL[f][0];
        int ny = FACE_NORMAL[f][1];
        int nz = FACE_NORMAL[f][2];

        int ux, uy, uz, vx, vy, vz;
        face_axes(f, &ux, &uy, &uz, &vx, &vy, &vz);

        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                int idx = f * 9 + r * 3 + c;
                int a = c - 1; // 沿 u 方向 -1..1
                int b = r - 1; // 沿 v 方向 -1..1
                // 位置 = 法向*1 + u*a + v*b
                s[idx].x = nx * 1 + ux * a + vx * b;
                s[idx].y = ny * 1 + uy * a + vy * b;
                s[idx].z = nz * 1 + uz * a + vz * b;
                s[idx].nx = nx; s[idx].ny = ny; s[idx].nz = nz;
            }
        }
    }
}

/* 绕轴旋转 90°，axis: 0=x, 1=y, 2=z；dir=+1 为右手定则正方向 */
static void rotate90(int *x, int *y, int *z, int axis, int dir) {
    int nx = *x, ny = *y, nz = *z;
    if (axis == 0) {
        if (dir > 0) { *y = -nz; *z =  ny; }
        else         { *y =  nz; *z = -ny; }
    } else if (axis == 1) {
        if (dir > 0) { *x =  nz; *z = -nx; }
        else         { *x = -nz; *z =  nx; }
    } else {
        if (dir > 0) { *x = -ny; *y =  nx; }
        else         { *x =  ny; *y = -nx; }
    }
}

/* 在 54 个 Sticker 中查找匹配 (pos, normal) 的索引 */
static int find_sticker(const Sticker s[54], int x, int y, int z,
                        int nx, int ny, int nz) {
    for (int i = 0; i < 54; i++) {
        if (s[i].x == x && s[i].y == y && s[i].z == z &&
            s[i].nx == nx && s[i].ny == ny && s[i].nz == nz)
            return i;
    }
    return -1;
}

/* 面 -> 旋转轴与方向（右手定则正方向为 +1）
 * 从面外侧看顺时针 = 绕“外法向轴”右手系的负方向转 90°：
 *   法向为正轴的面 (U/R/F) -> dir = -1
 *   法向为负轴的面 (D/L/B) -> dir = +1
 */
static void face_rotation(int f, int *axis, int *dir) {
    switch (f) {
        case 0: *axis = 1; *dir = -1; break; // U
        case 1: *axis = 0; *dir = -1; break; // R
        case 2: *axis = 2; *dir = -1; break; // F
        case 3: *axis = 1; *dir =  1; break; // D
        case 4: *axis = 0; *dir =  1; break; // L
        case 5: *axis = 2; *dir =  1; break; // B
    }
}

/* 组合两个置换：out[i] = a[b[i]] */
static void compose(const uint8_t a[54], const uint8_t b[54], uint8_t out[54]) {
    for (int i = 0; i < 54; i++)
        out[i] = a[b[i]];
}

/* 求逆置换 */
static void inverse(const uint8_t p[54], uint8_t out[54]) {
    for (int i = 0; i < 54; i++)
        out[p[i]] = (uint8_t)i;
}

void build_perm_tables(void) {
    Sticker s[54];
    build_stickers(s);

    for (int f = 0; f < 6; f++) {
        int axis, dir;
        face_rotation(f, &axis, &dir);

        // 该面所在的层：U/R/F 在 +1 层，D/L/B 在 -1 层
        int layer_val = (f == 0 || f == 1 || f == 2) ? 1 : -1;

        uint8_t base[54];
        for (int k = 0; k < 54; k++) base[k] = (uint8_t)k;

        for (int i = 0; i < 54; i++) {
            int px = s[i].x, py = s[i].y, pz = s[i].z;
            int coord = (axis == 0) ? px : (axis == 1) ? py : pz;
            if (coord != layer_val) continue;

            int rx = px, ry = py, rz = pz;
            int rnx = s[i].nx, rny = s[i].ny, rnz = s[i].nz;
            rotate90(&rx, &ry, &rz, axis, dir);
            rotate90(&rnx, &rny, &rnz, axis, dir);

            int j = find_sticker(s, rx, ry, rz, rnx, rny, rnz);
            if (j >= 0) {
                // 旧格子 i 旋转后到达新位置 j，因此 base[新位置 j] = 旧位置 i
                base[j] = (uint8_t)i;
            }
        }

        memcpy(perm[f * 3],     base, 54);        // 顺时针
        compose(base, base,     perm[f * 3 + 1]);// 180°
        inverse(base,           perm[f * 3 + 2]);// 逆时针
    }
}

void cube_init(uint8_t cube[54]) {
    for (int face = 0; face < 6; face++)
        for (int i = 0; i < 9; i++)
            cube[face * 9 + i] = (uint8_t)face;
}

void apply_move(uint8_t cube[54], int move_id) {
    uint8_t tmp[54];
    for (int i = 0; i < 54; i++)
        tmp[i] = cube[perm[move_id][i]];
    memcpy(cube, tmp, 54);
}

int is_solved(const uint8_t cube[54]) {
    for (int face = 0; face < 6; face++)
        for (int i = 1; i < 9; i++)
            if (cube[face * 9 + i] != cube[face * 9])
                return 0;
    return 1;
}

void print_cube(const uint8_t cube[54]) {
    const char *names = "URFDLB";
    printf("\n");
    for (int r = 0; r < 3; r++) {
        printf("      ");
        for (int c = 0; c < 3; c++)
            printf("%c ", names[cube[0 + r * 3 + c]]);
        printf("\n");
    }
    int faces[] = {4, 2, 1, 5}; // L F R B
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 4; k++) {
            int f = faces[k];
            for (int c = 0; c < 3; c++)
                printf("%c ", names[cube[f * 9 + r * 3 + c]]);
        }
        printf("\n");
    }
    for (int r = 0; r < 3; r++) {
        printf("      ");
        for (int c = 0; c < 3; c++)
            printf("%c ", names[cube[27 + r * 3 + c]]);
        printf("\n");
    }
    printf("\n");
}