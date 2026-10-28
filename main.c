#include "cube.h"
#include <stdio.h>
#include <string.h>

static int parse_move(const char *s) {
    // 支持 U, R, F, D, L, B 及后缀 ' 或 2
    char face = s[0];
    int base = -1;
    switch (face) {
        case 'U': base = 0; break;
        case 'R': base = 3; break;
        case 'F': base = 6; break;
        case 'D': base = 9; break;
        case 'L': base = 12; break;
        case 'B': base = 15; break;
        default: return -1;
    }
    if (s[1] == '\0') return base;          // 顺时针
    if (s[1] == '2') return base + 1;       // 180°
    if (s[1] == '\'') return base + 2;      // 逆时针
    return -1;
}

int main(void) {
    build_perm_tables();
    uint8_t cube[54];
    cube_init(cube);

    printf("Rubik's Cube Simulator (text mode)\n");
    printf("Commands: U R F D L B (add ' or 2 for variants)\n");
    printf("Type 'quit' to exit.\n\n");

    char cmd[16];
    while (1) {
        print_cube(cube);
        printf("Move> ");
        if (scanf("%15s", cmd) != 1) break;
        if (strcmp(cmd, "quit") == 0) break;
        int m = parse_move(cmd);
        if (m < 0) {
            printf("Unknown move: %s\n", cmd);
            continue;
        }
        apply_move(cube, m);
        if (is_solved(cube))
            printf("*** SOLVED ***\n");
    }
    return 0;
}