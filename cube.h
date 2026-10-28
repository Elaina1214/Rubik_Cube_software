#ifndef CUBE_H
#define CUBE_H

#include <stdint.h>

// 面索引：U=0, R=1, F=2, D=3, L=4, B=5
// 每个面内按行优先存储 9 个色块
#define FACE_U 0
#define FACE_R 1
#define FACE_F 2
#define FACE_D 3
#define FACE_L 4
#define FACE_B 5

// 18 种基本转动编号
// 0-2: U, U2, U'
// 3-5: R, R2, R'
// 6-8: F, F2, F'
// 9-11: D, D2, D'
// 12-14: L, L2, L'
// 15-17: B, B2, B'
#define MOVE_U  0
#define MOVE_U2 1
#define MOVE_UP 2
#define MOVE_R  3
#define MOVE_R2 4
#define MOVE_RP 5
#define MOVE_F  6
#define MOVE_F2 7
#define MOVE_FP 8
#define MOVE_D  9
#define MOVE_D2 10
#define MOVE_DP 11
#define MOVE_L  12
#define MOVE_L2 13
#define MOVE_LP 14
#define MOVE_B  15
#define MOVE_B2 16
#define MOVE_BP 17

extern uint8_t perm[18][54];

void cube_init(uint8_t cube[54]);
void build_perm_tables(void);
void apply_move(uint8_t cube[54], int move_id);
int is_solved(const uint8_t cube[54]);
void print_cube(const uint8_t cube[54]);

#endif