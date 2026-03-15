#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CコアとC++ UI層で共有する、メモリ上の画像コンテナです。 / In-memory image container shared between the C core and the C++ UI layer.
// `pixels` が指すヒープバッファの所有権はこの構造体が持ち、不要になったら img_free() で解放します。 / The struct owns the heap buffer pointed to by `pixels` and must be released with img_free() when no longer needed.
typedef struct {
  // 画像の横幅（ピクセル単位）です。 / Image width in pixels.
  int w;
  // 画像の縦幅（ピクセル単位）です。 / Image height in pixels.
  int h;
  // 1ピクセルあたりのチャンネル数です。 / Number of channels per pixel.
  // 代表例として、RGBA=4、RGB=3、グレースケール=1 があります。 / Typical values are 4 for RGBA, 3 for RGB, and 1 for grayscale.
  int channels;
  // ヒープ上に確保された生のピクセルバッファへのポインタです。 / Pointer to the raw pixel buffer on the heap.
  // メモリは stb_image またはリサイズ処理で確保され、img_free() を呼ぶまでこの Image が所有します。 / Memory is allocated by stb_image or by the resize routine and is owned by this Image instance until img_free() is called.
  uint8_t* pixels;
} Image;

// 画像を読み込み、RGBA（4チャンネル）に正規化します。 / Load an image and normalize it to RGBA (4 channels).
// 成功時は0、失敗時は負の値を返します。 / Returns 0 on success and a negative value on error.
int img_load_rgba(const char* path, Image* out);

// RGBA画像を指定した新しいサイズへリサイズします。 / Resize an RGBA image to new dimensions.
// 成功時は0、失敗時は負の値を返します。 / Returns 0 on success and a negative value on error.
int img_resize_rgba(const Image* src, int new_w, int new_h, Image* out);

// 画像をPNGとして保存します。 / Save an image as PNG.
// 成功時は0、失敗時は負の値を返します。 / Returns 0 on success and a negative value on error.
int img_save_png(const char* path, const Image* img);

// Image に関連付いたメモリを解放します。 / Release memory associated with an Image.
void img_free(Image* img);

#ifdef __cplusplus
}
#endif
