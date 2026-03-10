#include "image_core.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__has_include)
#  if __has_include("stb_image_resize2.h")
#    define IMAGETOOL_USE_STB_RESIZE2 1
#    define STB_IMAGE_RESIZE2_IMPLEMENTATION
#    include "stb_image_resize2.h"
#  elif __has_include("stb_image_resize.h")
#    define STB_IMAGE_RESIZE_IMPLEMENTATION
#    include "stb_image_resize.h"
#  else
#    error "Neither stb_image_resize2.h nor stb_image_resize.h is available"
#  endif
#else
#  define STB_IMAGE_RESIZE_IMPLEMENTATION
#  include "stb_image_resize.h"
#endif

static void img_reset(Image* img) {
  if (!img) {
    return;
  }

  img->w = 0;
  img->h = 0;
  img->channels = 0;
  img->pixels = NULL;
}

int img_load_rgba(const char* path, Image* out) {
  if (!path || !out) {
    return -1;
  }

  img_reset(out);

  int w = 0;
  int h = 0;
  int channels = 0;

  uint8_t* data = stbi_load(path, &w, &h, &channels, 4);
  if (!data) {
    return -2;
  }

  out->w = w;
  out->h = h;
  out->channels = 4;
  out->pixels = data;

  return 0;
}

int img_resize_rgba(const Image* src, int new_w, int new_h, Image* out) {
  if (!src || !src->pixels || !out) {
    return -1;
  }

  if (new_w <= 0 || new_h <= 0 || src->channels <= 0) {
    return -2;
  }

  img_reset(out);

  size_t pixel_count = (size_t)new_w * (size_t)new_h;
  if (pixel_count > (SIZE_MAX / (size_t)src->channels)) {
    return -3;
  }

  size_t bytes = pixel_count * (size_t)src->channels;
  if (bytes > (size_t)INT_MAX) {
    return -4;
  }

  uint8_t* dst = (uint8_t*)malloc(bytes);
  if (!dst) {
    return -5;
  }

#if defined(IMAGETOOL_USE_STB_RESIZE2)
  stbir_pixel_layout layout = STBIR_1CHANNEL;
  switch (src->channels) {
    case 1:
      layout = STBIR_1CHANNEL;
      break;
    case 2:
      layout = STBIR_2CHANNEL;
      break;
    case 3:
      layout = STBIR_RGB;
      break;
    case 4:
      layout = STBIR_RGBA;
      break;
    default:
      free(dst);
      return -7;
  }

  const int ok = stbir_resize_uint8_linear(
      src->pixels,
      src->w,
      src->h,
      0,
      dst,
      new_w,
      new_h,
      0,
      layout);
#else
  const int ok = stbir_resize_uint8(
      src->pixels,
      src->w,
      src->h,
      0,
      dst,
      new_w,
      new_h,
      0,
      src->channels);
#endif

  if (!ok) {
    free(dst);
    return -6;
  }

  out->w = new_w;
  out->h = new_h;
  out->channels = src->channels;
  out->pixels = dst;

  return 0;
}

int img_save_png(const char* path, const Image* img) {
  if (!path || !img || !img->pixels || img->w <= 0 || img->h <= 0 || img->channels <= 0) {
    return -1;
  }

  const int stride = img->w * img->channels;
  if (!stbi_write_png(path, img->w, img->h, img->channels, img->pixels, stride)) {
    return -2;
  }

  return 0;
}

void img_free(Image* img) {
  if (!img) {
    return;
  }

  if (img->pixels) {
    stbi_image_free(img->pixels);
  }

  img_reset(img);
}
