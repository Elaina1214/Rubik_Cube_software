#ifndef SOLVER_H
#define SOLVER_H

#include <stdint.h>

/* 启动时预计算求解表；solve_cube 内部也会自动调用一次，重复调用无副作用。 */
void solver_init(void);

/* 求解 cube_in（54 个贴纸，面序 U R F D L B，每个面 3x3 行优先）。
 * 成功返回步数（0 表示已经复原），把步数写入 moves_out；
 * 失败或超出 max_moves 返回 -1，且不会向 moves_out 写入任何内容。 */
int solve_cube(const uint8_t cube_in[54], int moves_out[], int max_moves);

#endif
