#include "input.h"
#include <gccore.h>

#define REPEAT_DELAY 22   /* frames before a held direction repeats */
#define REPEAT_RATE  5

static u32 held, pressed, prev;
static int hold_frames;

void input_init(void) { PAD_Init(); }

static u32 map(int chan) {
    u32 b = PAD_ButtonsHeld(chan), r = 0;
    if (b & PAD_BUTTON_A) r |= BTN_A;
    if (b & PAD_BUTTON_B) r |= BTN_B;
    if (b & PAD_BUTTON_X) r |= BTN_X;
    if (b & PAD_BUTTON_Y) r |= BTN_Y;
    if (b & PAD_BUTTON_START) r |= BTN_START;
    if (b & PAD_BUTTON_UP) r |= BTN_UP;
    if (b & PAD_BUTTON_DOWN) r |= BTN_DOWN;
    if (b & PAD_BUTTON_LEFT) r |= BTN_LEFT;
    if (b & PAD_BUTTON_RIGHT) r |= BTN_RIGHT;
    if (b & PAD_TRIGGER_L) r |= BTN_L;
    if (b & PAD_TRIGGER_R) r |= BTN_R;
    if (b & PAD_TRIGGER_Z) r |= BTN_Z;
    s8 sx = PAD_StickX(chan), sy = PAD_StickY(chan);
    if (sy > 40) r |= BTN_UP;
    if (sy < -40) r |= BTN_DOWN;
    if (sx < -40) r |= BTN_LEFT;
    if (sx > 40) r |= BTN_RIGHT;
    return r;
}

void input_poll(void) {
    PAD_ScanPads();
    held = 0;
    for (int i = 0; i < 4; i++) held |= map(i);
    pressed = held & ~prev;
    const u32 dirs = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;
    if (held & dirs) {
        if ((held & dirs) != (prev & dirs)) hold_frames = 0;
        else {
            hold_frames++;
            if (hold_frames >= REPEAT_DELAY && (hold_frames - REPEAT_DELAY) % REPEAT_RATE == 0)
                pressed |= held & dirs;
        }
    } else hold_frames = 0;
    prev = held;
}

u32 input_pressed(void) { return pressed; }
u32 input_held(void) { return held; }
