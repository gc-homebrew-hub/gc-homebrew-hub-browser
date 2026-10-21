/* osk.h - an on-screen keyboard for typing a web address with the pad. */
#pragma once
#include <stdbool.h>
#include <gctypes.h>

void osk_open(const char *title, const char *initial, int maxlen);
bool osk_active(void);
int  osk_update(u32 pressed);    /* 1 accepted, -1 cancelled, 0 still typing */
const char *osk_text(void);
void osk_draw(void);
