/* PC preview stand-in for FileIOC: AppVars become files in preview/out. */
#ifndef MOCK_FILEIOC_H
#define MOCK_FILEIOC_H
#include <stddef.h>
#include <stdint.h>

uint8_t ti_Open(const char *name, const char *mode);
int ti_Close(uint8_t handle);
size_t ti_Read(void *data, size_t size, size_t count, uint8_t handle);
size_t ti_Write(const void *data, size_t size, size_t count, uint8_t handle);
int ti_SetArchiveStatus(uint8_t archive, uint8_t handle);

#endif
