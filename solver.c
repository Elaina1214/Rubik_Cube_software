#include "solver.h"
#include "cube.h"

#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * 两阶段（Kociemba）魔方求解器
 *
 * 第一阶段：把魔方化简到 G1 子群（角块方向归零、棱块方向归零、
 *           中层 4 个棱块回到中层）。
 * 第二阶段：在 G1 内只用 U、D、R2、F2、L2、B2 复原整颗魔方。
 *
 * 所有转动定义直接由 cube.c 的 perm 表反推，因此求解结果与渲染、
 * 与 apply_move 完全一致，不存在"手写表对不上"的风险。
 *
 * 与旧实现的关键区别：
 *   - 结果长度固定上限（典型 20 步左右），写入 moves_out 前检查容量，
 *     不再出现越界写破坏 perm / cube 的情况。
 *   - 不再依赖"打分 + 盲目转 U"的爬山法，可以保证真正复原。
 * ===================================================================== */

#define N_TWIST   2187    /* 3^7   角块方向 */
#define N_FLIP    2048    /* 2^11  棱块方向 */
#define N_SLICE   495     /* C(12,4) 中层 4 棱的位置组合 */
#define N_CPERM   40320   /* 8!    角块排列 */
#define N_EPERM   40320   /* 8!    U/D 层 8 个棱块的排列 */
#define N_SPERM   24      /* 4!    中层 4 个棱块的排列 */
#define N_P2MOVE  10

#define MAX_SOLUTION  64
#define MAX_P1_DEPTH  12
#define MAX_P2_DEPTH  18

typedef struct {
    uint8_t cp[8], co[8];
    uint8_t ep[12], eo[12];
} Cubie;

/* 角块 / 棱块的贴纸位置，面序 U R F D L B，每个面 3x3 行优先 */
static const uint8_t CORNER_FACELET[8][3] = {
    { 8,  9, 20}, /* URF */
    { 6, 18, 38}, /* UFL */
    { 0, 36, 47}, /* ULB */
    { 2, 45, 11}, /* UBR */
    {29, 26, 15}, /* DFR */
    {27, 44, 24}, /* DLF */
    {33, 53, 42}, /* DBL */
    {35, 17, 51}, /* DRB */
};

/* 各角块三个贴纸的颜色（顺序与 CORNER_FACELET 一致） */
static const uint8_t CORNER_COLOR[8][3] = {
    {0, 1, 2}, {0, 2, 4}, {0, 4, 5}, {0, 5, 1},
    {3, 2, 1}, {3, 4, 2}, {3, 5, 4}, {3, 1, 5},
};

static const uint8_t EDGE_FACELET[12][2] = {
    { 5, 10}, /* UR */
    { 7, 19}, /* UF */
    { 3, 37}, /* UL */
    { 1, 46}, /* UB */
    {32, 16}, /* DR */
    {28, 25}, /* DF */
    {30, 43}, /* DL */
    {34, 52}, /* DB */
    {23, 12}, /* FR */
    {21, 41}, /* FL */
    {50, 39}, /* BL */
    {48, 14}, /* BR */
};

static const uint8_t EDGE_COLOR[12][2] = {
    {0, 1}, {0, 2}, {0, 4}, {0, 5},
    {3, 1}, {3, 2}, {3, 4}, {3, 5},
    {2, 1}, {2, 4}, {5, 4}, {5, 1},
};

static const int FACT[9] = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};

/* 第二阶段允许的转动：U U2 U' D D2 D' R2 F2 L2 B2（对应 cube.h 的编号） */
static const int P2_MOVES[N_P2MOVE] = {
    MOVE_U, MOVE_U2, MOVE_UP,
    MOVE_D, MOVE_D2, MOVE_DP,
    MOVE_R2, MOVE_F2, MOVE_L2, MOVE_B2
};

static Cubie move_cube[18];

static uint16_t twist_move[N_TWIST][18];
static uint16_t flip_move[N_FLIP][18];
static uint16_t slice_move[N_SLICE][18];
static uint16_t cp_move[N_CPERM][N_P2MOVE];
static uint16_t ep_move[N_EPERM][N_P2MOVE];
static uint8_t  sp_move[N_SPERM][N_P2MOVE];

static uint8_t slice_rep[N_SLICE][12];

static uint8_t dist_ts[N_TWIST * N_SLICE];
static uint8_t dist_fs[N_FLIP * N_SLICE];
static uint8_t dist_cp[N_CPERM * N_SPERM];
static uint8_t dist_ep[N_EPERM * N_SPERM];

static int solver_ready = 0;

/* ------------------------- 基本动作 ------------------------- */

static void cubie_solved(Cubie *c) {
    for (int i = 0; i < 8; i++) { c->cp[i] = (uint8_t)i; c->co[i] = 0; }
    for (int i = 0; i < 12; i++) { c->ep[i] = (uint8_t)i; c->eo[i] = 0; }
}

static void cubie_mul(const Cubie *a, const Cubie *b, Cubie *r) {
    for (int i = 0; i < 8; i++) {
        r->cp[i] = a->cp[b->cp[i]];
        r->co[i] = (uint8_t)((a->co[b->cp[i]] + b->co[i]) % 3);
    }
    for (int i = 0; i < 12; i++) {
        r->ep[i] = a->ep[b->ep[i]];
        r->eo[i] = (uint8_t)((a->eo[b->ep[i]] + b->eo[i]) % 2);
    }
}

/* 贴纸 54 格 -> 块表示 */
static int cubie_parse(const uint8_t f[54], Cubie *c) {
    for (int i = 0; i < 8; i++) {
        int ori = -1;
        for (int o = 0; o < 3; o++) {
            uint8_t col = f[CORNER_FACELET[i][o]];
            if (col == 0 || col == 3) { ori = o; break; }
        }
        if (ori < 0) return 0;
        uint8_t c1 = f[CORNER_FACELET[i][(ori + 1) % 3]];
        uint8_t c2 = f[CORNER_FACELET[i][(ori + 2) % 3]];
        int piece = -1;
        for (int j = 0; j < 8; j++) {
            if (CORNER_COLOR[j][1] == c1 && CORNER_COLOR[j][2] == c2) { piece = j; break; }
        }
        if (piece < 0) return 0;
        c->cp[i] = (uint8_t)piece;
        c->co[i] = (uint8_t)ori;
    }

    for (int i = 0; i < 12; i++) {
        int piece = -1, ori = 0;
        for (int o = 0; o < 2 && piece < 0; o++) {
            uint8_t c1 = f[EDGE_FACELET[i][o]];
            uint8_t c2 = f[EDGE_FACELET[i][(o + 1) % 2]];
            for (int j = 0; j < 12; j++) {
                if (EDGE_COLOR[j][0] == c1 && EDGE_COLOR[j][1] == c2) {
                    piece = j; ori = o; break;
                }
            }
        }
        if (piece < 0) return 0;
        c->ep[i] = (uint8_t)piece;
        c->eo[i] = (uint8_t)ori;
    }
    return 1;
}

/* 块表示 -> 贴纸 54 格 */
static void cubie_to_facelet(const Cubie *c, uint8_t f[54]) {
    for (int i = 0; i < 8; i++) {
        int piece = c->cp[i], ori = c->co[i];
        for (int n = 0; n < 3; n++)
            f[CORNER_FACELET[i][(n + ori) % 3]] = CORNER_COLOR[piece][n];
    }
    for (int i = 0; i < 12; i++) {
        int piece = c->ep[i], ori = c->eo[i];
        for (int n = 0; n < 2; n++)
            f[EDGE_FACELET[i][(n + ori) % 2]] = EDGE_COLOR[piece][n];
    }
    f[4] = 0; f[13] = 1; f[22] = 2; f[31] = 3; f[40] = 4; f[49] = 5;
}

/* ------------------------- 坐标 ------------------------- */

static int get_twist(const Cubie *c) {
    int r = 0;
    for (int i = 0; i < 7; i++) r = r * 3 + c->co[i];
    return r;
}

static void set_twist(Cubie *c, int idx) {
    int sum = 0;
    for (int i = 6; i >= 0; i--) { c->co[i] = (uint8_t)(idx % 3); sum += c->co[i]; idx /= 3; }
    c->co[7] = (uint8_t)((3 - sum % 3) % 3);
}

static int get_flip(const Cubie *c) {
    int r = 0;
    for (int i = 0; i < 11; i++) r = r * 2 + c->eo[i];
    return r;
}

static void set_flip(Cubie *c, int idx) {
    int sum = 0;
    for (int i = 10; i >= 0; i--) { c->eo[i] = (uint8_t)(idx & 1); sum += c->eo[i]; idx >>= 1; }
    c->eo[11] = (uint8_t)(sum & 1);
}

static int get_perm8(const uint8_t p[8]) {
    int idx = 0;
    for (int i = 0; i < 8; i++) {
        int cnt = 0;
        for (int j = i + 1; j < 8; j++) if (p[j] < p[i]) cnt++;
        idx += cnt * FACT[7 - i];
    }
    return idx;
}

static void set_perm8(uint8_t p[8], int idx) {
    uint8_t avail[8];
    for (int i = 0; i < 8; i++) avail[i] = (uint8_t)i;
    for (int i = 0; i < 8; i++) {
        int f = FACT[7 - i];
        int k = idx / f;
        idx %= f;
        p[i] = avail[k];
        for (int j = k; j < 7 - i; j++) avail[j] = avail[j + 1];
    }
}

static int get_sp(const uint8_t ep[12]) {
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        int v = ep[8 + i] - 8;
        int cnt = 0;
        for (int j = i + 1; j < 4; j++) if (ep[8 + j] - 8 < v) cnt++;
        idx += cnt * FACT[3 - i];
    }
    return idx;
}

static void set_sp(Cubie *c, int idx) {
    uint8_t avail[4] = {8, 9, 10, 11};
    for (int i = 0; i < 4; i++) {
        int f = FACT[3 - i];
        int k = idx / f;
        idx %= f;
        c->ep[8 + i] = avail[k];
        for (int j = k; j < 3 - i; j++) avail[j] = avail[j + 1];
    }
}

static int binom(int n, int k) {
    if (k < 0 || n < 0 || k > n) return 0;
    int r = 1;
    for (int i = 0; i < k; i++) r = r * (n - i) / (i + 1);
    return r;
}

/* 中层 4 个棱块（编号 8..11）所在的 4 个位置 -> 0..494 */
static int get_slice(const uint8_t ep[12]) {
    int a = 0, x = 0;
    for (int j = 11; j >= 0; j--) {
        if (ep[j] >= 8) {
            a += binom(11 - j, x + 1);
            x++;
        }
    }
    return a;
}

/* ------------------------- 表构建 ------------------------- */

static void build_move_cubes(void) {
    uint8_t f0[54], f1[54];
    cube_init(f0);
    for (int m = 0; m < 18; m++) {
        memcpy(f1, f0, 54);
        apply_move(f1, m);
        cubie_parse(f1, &move_cube[m]);
    }
}

static void build_slice_rep(void) {
    for (int mask = 0; mask < (1 << 12); mask++) {
        int bits = 0;
        for (int j = 0; j < 12; j++) if (mask & (1 << j)) bits++;
        if (bits != 4) continue;

        uint8_t ep[12];
        int slice = 0, other = 0;
        for (int j = 0; j < 12; j++) {
            if (mask & (1 << j)) ep[j] = (uint8_t)(8 + slice++);
            else                 ep[j] = (uint8_t)(other++);
        }
        int idx = get_slice(ep);
        memcpy(slice_rep[idx], ep, 12);
    }
}

static void build_coord_moves(void) {
    Cubie base, r;

    for (int t = 0; t < N_TWIST; t++) {
        cubie_solved(&base);
        set_twist(&base, t);
        for (int m = 0; m < 18; m++) {
            cubie_mul(&base, &move_cube[m], &r);
            twist_move[t][m] = (uint16_t)get_twist(&r);
        }
    }

    for (int f = 0; f < N_FLIP; f++) {
        cubie_solved(&base);
        set_flip(&base, f);
        for (int m = 0; m < 18; m++) {
            cubie_mul(&base, &move_cube[m], &r);
            flip_move[f][m] = (uint16_t)get_flip(&r);
        }
    }

    for (int s = 0; s < N_SLICE; s++) {
        cubie_solved(&base);
        memcpy(base.ep, slice_rep[s], 12);
        for (int m = 0; m < 18; m++) {
            cubie_mul(&base, &move_cube[m], &r);
            slice_move[s][m] = (uint16_t)get_slice(r.ep);
        }
    }

    for (int p = 0; p < N_CPERM; p++) {
        cubie_solved(&base);
        set_perm8(base.cp, p);
        for (int i = 0; i < N_P2MOVE; i++) {
            cubie_mul(&base, &move_cube[P2_MOVES[i]], &r);
            cp_move[p][i] = (uint16_t)get_perm8(r.cp);
        }
    }

    for (int p = 0; p < N_EPERM; p++) {
        cubie_solved(&base);
        set_perm8(base.ep, p);
        for (int i = 0; i < N_P2MOVE; i++) {
            cubie_mul(&base, &move_cube[P2_MOVES[i]], &r);
            ep_move[p][i] = (uint16_t)get_perm8(r.ep);
        }
    }

    for (int p = 0; p < N_SPERM; p++) {
        cubie_solved(&base);
        set_sp(&base, p);
        for (int i = 0; i < N_P2MOVE; i++) {
            cubie_mul(&base, &move_cube[P2_MOVES[i]], &r);
            sp_move[p][i] = (uint8_t)get_sp(r.ep);
        }
    }
}

static void bfs_dist_ts(void) {
    const int total = N_TWIST * N_SLICE;
    memset(dist_ts, 0xFF, (size_t)total);
    int *queue = (int *)malloc((size_t)total * sizeof(int));
    if (!queue) return;

    int head = 0, tail = 0;
    dist_ts[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int idx = queue[head++];
        int tw = idx / N_SLICE;
        int sl = idx % N_SLICE;
        int d = dist_ts[idx] + 1;
        for (int m = 0; m < 18; m++) {
            int ni = twist_move[tw][m] * N_SLICE + slice_move[sl][m];
            if (dist_ts[ni] == 0xFF) { dist_ts[ni] = (uint8_t)d; queue[tail++] = ni; }
        }
    }
    free(queue);
}

static void bfs_dist_fs(void) {
    const int total = N_FLIP * N_SLICE;
    memset(dist_fs, 0xFF, (size_t)total);
    int *queue = (int *)malloc((size_t)total * sizeof(int));
    if (!queue) return;

    int head = 0, tail = 0;
    dist_fs[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int idx = queue[head++];
        int fl = idx / N_SLICE;
        int sl = idx % N_SLICE;
        int d = dist_fs[idx] + 1;
        for (int m = 0; m < 18; m++) {
            int ni = flip_move[fl][m] * N_SLICE + slice_move[sl][m];
            if (dist_fs[ni] == 0xFF) { dist_fs[ni] = (uint8_t)d; queue[tail++] = ni; }
        }
    }
    free(queue);
}

static void bfs_dist_cp(void) {
    const int total = N_CPERM * N_SPERM;
    memset(dist_cp, 0xFF, (size_t)total);
    int *queue = (int *)malloc((size_t)total * sizeof(int));
    if (!queue) return;

    int head = 0, tail = 0;
    dist_cp[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int idx = queue[head++];
        int cp = idx / N_SPERM;
        int sp = idx % N_SPERM;
        int d = dist_cp[idx] + 1;
        for (int i = 0; i < N_P2MOVE; i++) {
            int ni = cp_move[cp][i] * N_SPERM + sp_move[sp][i];
            if (dist_cp[ni] == 0xFF) { dist_cp[ni] = (uint8_t)d; queue[tail++] = ni; }
        }
    }
    free(queue);
}

static void bfs_dist_ep(void) {
    const int total = N_EPERM * N_SPERM;
    memset(dist_ep, 0xFF, (size_t)total);
    int *queue = (int *)malloc((size_t)total * sizeof(int));
    if (!queue) return;

    int head = 0, tail = 0;
    dist_ep[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int idx = queue[head++];
        int ep = idx / N_SPERM;
        int sp = idx % N_SPERM;
        int d = dist_ep[idx] + 1;
        for (int i = 0; i < N_P2MOVE; i++) {
            int ni = ep_move[ep][i] * N_SPERM + sp_move[sp][i];
            if (dist_ep[ni] == 0xFF) { dist_ep[ni] = (uint8_t)d; queue[tail++] = ni; }
        }
    }
    free(queue);
}

void solver_init(void) {
    if (solver_ready) return;
    build_perm_tables();
    build_move_cubes();
    build_slice_rep();
    build_coord_moves();
    bfs_dist_ts();
    bfs_dist_fs();
    bfs_dist_cp();
    bfs_dist_ep();
    solver_ready = 1;
}

/* ------------------------- 搜索 ------------------------- */

static int p1_path[MAX_SOLUTION];
static int p1_len = 0;
static int p2_path[MAX_SOLUTION];
static int p2_len = 0;

static int p2_dfs(int togo, int cp, int ep, int sp, int prev_face) {
    if (togo == 0) return (cp == 0 && ep == 0 && sp == 0);
    if (dist_cp[cp * N_SPERM + sp] > togo) return 0;
    if (dist_ep[ep * N_SPERM + sp] > togo) return 0;

    for (int i = 0; i < N_P2MOVE; i++) {
        int m = P2_MOVES[i];
        int face = m / 3;
        if (face == prev_face) continue;
        p2_path[p2_len++] = m;
        if (p2_dfs(togo - 1, cp_move[cp][i], ep_move[ep][i], sp_move[sp][i], face))
            return 1;
        p2_len--;
    }
    return 0;
}

static int solve_phase2(const Cubie *c) {
    int cp = get_perm8(c->cp);
    int ep = get_perm8(c->ep);
    int sp = get_sp(c->ep);

    int h = dist_cp[cp * N_SPERM + sp];
    int h2 = dist_ep[ep * N_SPERM + sp];
    if (h2 > h) h = h2;

    for (int d = h; d <= MAX_P2_DEPTH; d++) {
        p2_len = 0;
        if (p2_dfs(d, cp, ep, sp, -1)) return 1;
    }
    return 0;
}

static int p1_dfs(int togo, int tw, int fl, int sl, int prev_face, const Cubie *cc) {
    if (togo == 0) {
        if (tw != 0 || fl != 0 || sl != 0) return 0;
        return solve_phase2(cc);
    }

    int h = dist_ts[tw * N_SLICE + sl];
    int h2 = dist_fs[fl * N_SLICE + sl];
    if (h2 > h) h = h2;
    if (h > togo) return 0;

    for (int m = 0; m < 18; m++) {
        int face = m / 3;
        if (face == prev_face) continue;
        Cubie next;
        cubie_mul(cc, &move_cube[m], &next);
        p1_path[p1_len++] = m;
        if (p1_dfs(togo - 1, twist_move[tw][m], flip_move[fl][m], slice_move[sl][m],
                   face, &next))
            return 1;
        p1_len--;
    }
    return 0;
}

static int search(const Cubie *c, int *out) {
    int tw = get_twist(c);
    int fl = get_flip(c);
    int sl = get_slice(c->ep);

    int h = dist_ts[tw * N_SLICE + sl];
    int h2 = dist_fs[fl * N_SLICE + sl];
    if (h2 > h) h = h2;

    for (int d1 = h; d1 <= MAX_P1_DEPTH; d1++) {
        p1_len = 0;
        p2_len = 0;
        if (p1_dfs(d1, tw, fl, sl, -1, c)) {
            int total = p1_len + p2_len;
            if (total > MAX_SOLUTION) return -1;
            for (int i = 0; i < p1_len; i++) out[i] = p1_path[i];
            for (int i = 0; i < p2_len; i++) out[p1_len + i] = p2_path[i];
            return total;
        }
    }
    return -1;
}

/* ------------------------- 合法性校验 ------------------------- */

static int perm_parity(const uint8_t *p, int n) {
    int inv = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (p[j] < p[i]) inv++;
    return inv & 1;
}

static int cubie_valid(const Cubie *c) {
    uint8_t seen_c[8] = {0}, seen_e[12] = {0};
    int sum_co = 0, sum_eo = 0;

    for (int i = 0; i < 8; i++) {
        if (c->cp[i] > 7 || c->co[i] > 2 || seen_c[c->cp[i]]) return 0;
        seen_c[c->cp[i]] = 1;
        sum_co += c->co[i];
    }
    for (int i = 0; i < 12; i++) {
        if (c->ep[i] > 11 || c->eo[i] > 1 || seen_e[c->ep[i]]) return 0;
        seen_e[c->ep[i]] = 1;
        sum_eo += c->eo[i];
    }
    if (sum_co % 3 != 0) return 0;
    if (sum_eo % 2 != 0) return 0;
    if (perm_parity(c->cp, 8) != perm_parity(c->ep, 12)) return 0;
    return 1;
}

/* ------------------------- 对外接口 ------------------------- */

int solve_cube(const uint8_t cube_in[54], int moves_out[], int max_moves) {
    if (!moves_out || max_moves <= 0) return -1;

    if (is_solved(cube_in)) return 0;

    solver_init();

    Cubie c;
    if (!cubie_parse(cube_in, &c)) return -1;
    if (!cubie_valid(&c)) return -1;

    /* 反向序列化必须还原输入，否则说明贴纸排列本身不自洽 */
    {
        uint8_t f[54];
        cubie_to_facelet(&c, f);
        if (memcmp(f, cube_in, 54) != 0) return -1;
    }

    int sol[MAX_SOLUTION];
    int n = search(&c, sol);
    if (n < 0) return -1;

    /* 将求解器内部的半圈动作拆成两个独立的四分之一圈动作。
     * 例如 MOVE_R2 -> MOVE_R, MOVE_R；这样动画队列不会把 R2
     * 当成一次 180° 转动，用户可以逐步观察并计数每一次 90° 转动。
     * 先计算展开后的长度，确保容量不足时不会部分写入输出数组。 */
    int expanded_len = 0;
    for (int i = 0; i < n; i++) {
        expanded_len += (sol[i] % 3 == 1) ? 2 : 1;
    }
    if (expanded_len > max_moves) return -1;

    int out_idx = 0;
    for (int i = 0; i < n; i++) {
        int move = sol[i];
        if (move % 3 == 1) {
            /* U2/R2/F2/D2/L2/B2 -> 对应面的两次顺时针 90° */
            int quarter_turn = (move / 3) * 3;
            moves_out[out_idx++] = quarter_turn;
            moves_out[out_idx++] = quarter_turn;
        } else {
            moves_out[out_idx++] = move;
        }
    }
    return out_idx;
}
