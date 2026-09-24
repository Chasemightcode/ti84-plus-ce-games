/* PC preview: replace clock() with a simulated 32768 Hz clock. */
#ifndef MOCK_TIME_H
#define MOCK_TIME_H
#include_next <time.h>
unsigned long mock_clock(void);
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 32768UL
#define clock() mock_clock()
#endif
