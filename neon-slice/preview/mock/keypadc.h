/* PC preview stand-in for KeypadC. */
#ifndef MOCK_KEYPADC_H
#define MOCK_KEYPADC_H
#include <stdint.h>

extern uint8_t mock_kb_data[8];
#define kb_Data mock_kb_data
void kb_Scan(void);

#define kb_2nd      (1 << 5)
#define kb_Mode     (1 << 6)
#define kb_Del      (1 << 7)
#define kb_Enter    (1 << 0)
#define kb_Clear    (1 << 6)
#define kb_Down     (1 << 0)
#define kb_Left     (1 << 1)
#define kb_Right    (1 << 2)
#define kb_Up       (1 << 3)

#endif
