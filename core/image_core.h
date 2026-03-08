#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int w;
  int h;
  int channels;
  uint8_t* pixels;
} Image;

// Load an image and normalize to RGBA (4 channels).
// Returns 0 on success, negative value on error.
int img_load_rgba(const char* path, Image* out);

// Resize RGBA image to new dimensions.
// Returns 0 on success, negative value on error.
int img_resize_rgba(const Image* src, int new_w, int new_h, Image* out);

// Save image as PNG.
// Returns 0 on success, negative value on error.
int img_save_png(const char* path, const Image* img);

// Release memory associated with an Image.
void img_free(Image* img);

#ifdef __cplusplus
}
#endif
