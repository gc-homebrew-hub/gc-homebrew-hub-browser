/* input.h - GameCube pads merged into one set of buttons, with auto-repeat
 * on the directions (D-pad and Control Stick both count). */
#pragma once
#include <stdbool.h>
#include <gctypes.h>

enum {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3, BTN_START = 1 << 4,
    BTN_UP = 1 << 5, BTN_DOWN = 1 << 6, BTN_LEFT = 1 << 7, BTN_RIGHT = 1 << 8,
    BTN_L = 1 << 9, BTN_R = 1 << 10, BTN_Z = 1 << 11,
};

void input_init(void);
void input_poll(void);          /* once per frame */
u32  input_pressed(void);       /* buttons that went down this frame, plus direction repeats */
u32  input_held(void);
