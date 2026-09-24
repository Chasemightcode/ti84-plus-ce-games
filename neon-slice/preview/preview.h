/* Shared declarations for the PC preview harness. */
#ifndef PREVIEW_H
#define PREVIEW_H
#include <stdint.h>

extern const char *preview_out_dir;
extern int mock_frame;
extern int mock_warnings;

void preview_frame_done(uint8_t fb[240][320], const uint16_t *palette);
void preview_keys(uint8_t kb[8]);
int preview_measure(void);
int write_png(const char *path, const uint8_t *rgb, int w, int h);

#endif
