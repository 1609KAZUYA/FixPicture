// C++ UI層と共有する公開API宣言を取り込みます。 / Include the public Image type and function declarations shared with the C++ UI layer.
#include "image_core.h"

#include <ctype.h>
// 計算したバッファサイズが stb の int ベースAPIで扱える範囲か確認するために使います。 / INT_MAX is used when checking whether the computed buffer size still fits into the integer-based APIs exposed by stb.
#include <limits.h>
// 安全なバイト数計算のために size_t と SIZE_MAX を使います。 / size_t and SIZE_MAX are used for safe byte-count calculations.
#include <stddef.h>
// リサイズ後の出力バッファを確保・解放するために malloc と free を使います。 / malloc and free are used for the destination resize buffer.
#include <stdlib.h>
#include <stdio.h>
// 将来このファイルで低レベルなメモリ操作が増えた場合に備えて含めています。 / Kept available for low-level memory utilities if this file grows later.
#include <string.h>

#if defined(__APPLE__)
#  include <ApplicationServices/ApplicationServices.h>
#endif

// stb_image の実装本体をこの翻訳単位で有効化し、別の .c を用意せず画像読込関数を使えるようにします。 / Emit stb_image implementation in this translation unit so image loading functions become available without a separate .c file from stb.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// PNG書き出しのために stb_image_write の実装本体もここで有効化します。 / Emit stb_image_write implementation here for PNG export support.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__has_include)
#  if __has_include("stb_image_resize2.h")
    // 新しい stb では v2 API が入ることがあるため、利用可能なら stb_image_resize2 を優先します。 / Prefer stb_image_resize2 when available because newer stb snapshots may ship the v2 API instead of the legacy header.
#    define IMAGETOOL_USE_STB_RESIZE2 1
#    define STB_IMAGE_RESIZE2_IMPLEMENTATION
#    include "stb_image_resize2.h"
#  elif __has_include("stb_image_resize.h")
    // resize2 が無い環境では従来のリサイズAPIへフォールバックします。 / Fall back to the classic resize API when resize2 is not present.
#    define STB_IMAGE_RESIZE_IMPLEMENTATION
#    include "stb_image_resize.h"
#  else
#    error "Neither stb_image_resize2.h nor stb_image_resize.h is available"
#  endif
#else
  // __has_include を持たないコンパイラでは従来ヘッダを直接使います。 / Compilers without __has_include use the legacy header path directly.
#  define STB_IMAGE_RESIZE_IMPLEMENTATION
#  include "stb_image_resize.h"
#endif

// すべてのフィールドを初期状態へ戻し、エラー時や解放後に中途半端な状態を見せないようにします。 / Reset every field in Image so callers never observe partially initialized state after an error path or after explicit cleanup.
static void img_reset(Image* img) {
  // NULL が来ても安全に何もしないようにします。 / Ignore NULL input so callers can safely forward optional pointers.
  if (!img) {
    return;
  }

  // 読み込み済みまたはリサイズ済み画像の横幅です。 / Pixel width of the loaded/resized image.
  img->w = 0;
  // 読み込み済みまたはリサイズ済み画像の縦幅です。 / Pixel height of the loaded/resized image.
  img->h = 0;
  // ピクセルバッファに入っている色チャンネル数です。 / Number of color channels stored in pixels.
  img->channels = 0;
  // Image が所有するヒープバッファです。NULL は画像データ未保持を意味します。 / Heap buffer owned by Image. NULL means "no image data".
  img->pixels = NULL;
}

// path が特定の拡張子を持つか、小文字比較で確認します。 / Check whether the path has the requested extension using a lowercase comparison.
static int img_path_has_extension(const char* path, const char* extension) {
  if (!path || !extension) {
    return 0;
  }

  const char* dot = strrchr(path, '.');
  if (!dot) {
    return 0;
  }

  while (*dot && *extension) {
    if (tolower((unsigned char)*dot) != tolower((unsigned char)*extension)) {
      return 0;
    }
    ++dot;
    ++extension;
  }

  return (*dot == '\0' && *extension == '\0');
}

// JPEG/PDF 書き出しで使う RGB バッファへ変換します。 / Convert an image to an RGB buffer used by JPEG/PDF export.
static uint8_t* img_copy_rgb(const Image* img) {
  if (!img || !img->pixels || img->w <= 0 || img->h <= 0 || img->channels <= 0) {
    return NULL;
  }

  const size_t pixel_count = (size_t)img->w * (size_t)img->h;
  if (pixel_count > (SIZE_MAX / 3u)) {
    return NULL;
  }

  uint8_t* rgb = (uint8_t*)malloc(pixel_count * 3u);
  if (!rgb) {
    return NULL;
  }

  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t src_index = i * (size_t)img->channels;
    const size_t dst_index = i * 3u;

    rgb[dst_index + 0] = img->pixels[src_index + 0];
    rgb[dst_index + 1] = (img->channels >= 2) ? img->pixels[src_index + 1] : img->pixels[src_index + 0];
    rgb[dst_index + 2] = (img->channels >= 3) ? img->pixels[src_index + 2] : img->pixels[src_index + 0];
  }

  return rgb;
}

// JPEG 品質値を stb が扱いやすい範囲へ丸めます。 / Clamp JPEG quality into the range expected by stb.
static int img_normalize_quality(int quality) {
  if (quality < 1) {
    return 1;
  }
  if (quality > 100) {
    return 100;
  }
  return quality;
}

typedef struct {
  uint8_t* data;
  size_t size;
  size_t capacity;
} ImgByteBuffer;

// 可変長バッファへ追記します。 / Append bytes to a growable buffer.
static int img_buffer_append(ImgByteBuffer* buffer, const void* data, size_t data_size) {
  if (!buffer || (!data && data_size != 0)) {
    return 0;
  }

  if (data_size == 0) {
    return 1;
  }

  if (buffer->size > (SIZE_MAX - data_size)) {
    return 0;
  }

  const size_t required = buffer->size + data_size;
  if (required > buffer->capacity) {
    size_t new_capacity = (buffer->capacity == 0) ? 4096u : buffer->capacity;
    while (new_capacity < required) {
      if (new_capacity > (SIZE_MAX / 2u)) {
        new_capacity = required;
        break;
      }
      new_capacity *= 2u;
    }

    uint8_t* new_data = (uint8_t*)realloc(buffer->data, new_capacity);
    if (!new_data) {
      return 0;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
  }

  memcpy(buffer->data + buffer->size, data, data_size);
  buffer->size += data_size;
  return 1;
}

// stb_image_write の callback から JPEG データを収集します。 / Collect JPEG bytes emitted by stb_image_write callbacks.
static void img_stbi_write_to_buffer(void* context, void* data, int size) {
  ImgByteBuffer* buffer = (ImgByteBuffer*)context;
  if (!buffer || size <= 0) {
    return;
  }

  img_buffer_append(buffer, data, (size_t)size);
}

// PDF の object offset を記録しながら文字列を書き出します。 / Write formatted text while tracking PDF object offsets.
static int img_pdf_write_bytes(FILE* file, long* offset, const void* data, size_t size) {
  if (!file || !offset || (!data && size != 0)) {
    return 0;
  }

  if (size != 0 && fwrite(data, 1u, size, file) != size) {
    return 0;
  }

  *offset += (long)size;
  return 1;
}

static int img_pdf_write_text(FILE* file, long* offset, const char* text) {
  return img_pdf_write_bytes(file, offset, text, strlen(text));
}

#if defined(__APPLE__)
// macOS では Quartz で PDF 1ページ目を RGBA へ描画します。 / On macOS, rasterize the first page of a PDF into RGBA via Quartz.
static int img_load_pdf_rgba(const char* path, Image* out) {
  CFURLRef url = NULL;
  CGPDFDocumentRef document = NULL;
  CGColorSpaceRef color_space = NULL;
  CGContextRef context = NULL;
  uint8_t* data = NULL;

  url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path, (CFIndex)strlen(path), false);
  if (!url) {
    return -3;
  }

  document = CGPDFDocumentCreateWithURL(url);
  if (!document) {
    CFRelease(url);
    return -4;
  }

  CGPDFPageRef page = CGPDFDocumentGetPage(document, 1);
  if (!page) {
    CGPDFDocumentRelease(document);
    CFRelease(url);
    return -5;
  }

  CGRect media_box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
  const int width = (int)media_box.size.width;
  const int height = (int)media_box.size.height;
  if (width <= 0 || height <= 0) {
    CGPDFDocumentRelease(document);
    CFRelease(url);
    return -6;
  }

  const size_t bytes_per_row = (size_t)width * 4u;
  data = (uint8_t*)calloc((size_t)height, bytes_per_row);
  if (!data) {
    CGPDFDocumentRelease(document);
    CFRelease(url);
    return -7;
  }

  color_space = CGColorSpaceCreateDeviceRGB();
  if (!color_space) {
    free(data);
    CGPDFDocumentRelease(document);
    CFRelease(url);
    return -8;
  }

  context = CGBitmapContextCreate(
      data,
      (size_t)width,
      (size_t)height,
      8u,
      bytes_per_row,
      color_space,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  if (!context) {
    CGColorSpaceRelease(color_space);
    free(data);
    CGPDFDocumentRelease(document);
    CFRelease(url);
    return -9;
  }

  CGContextSetRGBFillColor(context, 1.0, 1.0, 1.0, 1.0);
  CGContextFillRect(context, CGRectMake(0.0, 0.0, (CGFloat)width, (CGFloat)height));
  CGContextTranslateCTM(context, 0.0, (CGFloat)height);
  CGContextScaleCTM(context, 1.0, -1.0);
  CGContextDrawPDFPage(context, page);

  out->w = width;
  out->h = height;
  out->channels = 4;
  out->pixels = data;

  CGContextRelease(context);
  CGColorSpaceRelease(color_space);
  CGPDFDocumentRelease(document);
  CFRelease(url);
  return 0;
}
#endif

int img_load_rgba(const char* path, Image* out) {
  // path: 読み込む元画像のファイルパスです。 / path: filesystem path to the source image.
  // out: デコード結果を受け取る呼び出し側所有の Image 構造体です。 / out: caller-owned Image struct that receives the decoded result.
  if (!path || !out) {
    return -1;
  }

  // 呼び出し側が古い Image を再利用していても安全なように、まず空状態へ戻します。 / Start from a known empty state in case the caller reused an old Image.
  img_reset(out);

  if (img_path_has_extension(path, ".pdf")) {
#if defined(__APPLE__)
    return img_load_pdf_rgba(path, out);
#else
    return -10;
#endif
  }

  // w: stb_image が返すデコード後の横幅です。 / w: decoded source width returned by stb_image.
  int w = 0;
  // h: stb_image が返すデコード後の縦幅です。 / h: decoded source height returned by stb_image.
  int h = 0;
  // channels: RGBA固定化する前の元画像のチャンネル数です。 / channels: original channel count before forcing RGBA output.
  int channels = 0;

  // stbi_load はファイルをデコードし、ピクセル用ヒープバッファを確保します。 / stbi_load decodes the file and allocates a heap buffer.
  // 最後の引数に 4 を渡すことで出力を常に RGBA に揃え、後続処理を単純化します。 / The final argument forces 4 output channels, so every loaded image is normalized to RGBA for simpler downstream handling.
  uint8_t* data = stbi_load(path, &w, &h, &channels, 4);
  if (!data) {
    return -2;
  }

  // 取得した画像メタデータとバッファを呼び出し側の Image に格納します。 / Store the decoded metadata and buffer in the caller's Image object.
  out->w = w;
  out->h = h;
  out->channels = 4;
  out->pixels = data;

  // このAPI群では 0 を成功コードとして統一しています。 / 0 is the success code used consistently across this API.
  return 0;
}

int img_resize_rgba(const Image* src, int new_w, int new_h, Image* out) {
  // src: すでにメモリ上へ読み込まれている元画像です。 / src: source image already loaded in memory.
  // new_w/new_h: 出力したい新しいサイズです。 / new_w/new_h: requested output size in pixels.
  // out: リサイズ後画像を受け取り、確保された出力バッファを所有します。 / out: receives the resized image and owns the allocated destination buffer.
  if (!src || !src->pixels || !out) {
    return -1;
  }

  // 不正なサイズやチャンネル数は早い段階で弾きます。 / Reject invalid geometry and invalid channel counts early.
  if (new_w <= 0 || new_h <= 0 || src->channels <= 0) {
    return -2;
  }

  // メモリ確保前に出力先を空状態へ初期化しておきます。 / Clear the destination object before any allocation attempt.
  img_reset(out);

  // pixel_count: リサイズ後画像に含まれる総ピクセル数です。 / pixel_count: total number of pixels in the resized image.
  size_t pixel_count = (size_t)new_w * (size_t)new_h;
  // `pixel_count * channels` でオーバーフローしないか確認します。 / Guard against overflow in "pixel_count * channels".
  if (pixel_count > (SIZE_MAX / (size_t)src->channels)) {
    return -3;
  }

  // bytes: 出力バッファに必要な総バイト数です。 / bytes: number of bytes required for the resized output buffer.
  size_t bytes = pixel_count * (size_t)src->channels;
  // stb 側に int サイズ前提のAPIがあるため、実用上その上限を超えるサイズは拒否します。 / Some stb entry points still use int-sized parameters, so reject sizes that exceed that practical limit.
  if (bytes > (size_t)INT_MAX) {
    return -4;
  }

  // dst: 成功時にリサイズ後ピクセルを書き込むヒープバッファです。 / dst: heap buffer that will hold the resized pixels on success.
  uint8_t* dst = (uint8_t*)malloc(bytes);
  if (!dst) {
    return -5;
  }

#if defined(IMAGETOOL_USE_STB_RESIZE2)
  // resize2 では単なるチャンネル数ではなくレイアウト列挙値が必要なので変換します。 / resize2 selects channel layout explicitly instead of taking raw channel count, so map the Image metadata to the appropriate enum.
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
      // resize2 API が扱えないレイアウトなので、所有権を明確にするためバッファを解放して返します。 / Unsupported layout for the resize2 API. Release the buffer before returning so ownership stays clear.
      free(dst);
      return -7;
  }

  // resized_pixels: 成功時は出力先バッファ dst を返し、失敗時は NULL を返します。 / resized_pixels: returns the destination buffer on success and NULL on failure.
  unsigned char* resized_pixels = stbir_resize_uint8_linear(
      src->pixels,
      src->w,
      src->h,
      0,
      dst,
      new_w,
      new_h,
      0,
      layout);
  const int ok = (resized_pixels != NULL);
#else
  // 従来APIではチャンネル数をそのまま渡します。 / Legacy resize API accepts the channel count directly.
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

  // 失敗時の dst は使えないため、この場で必ず解放します。 / Any non-success result means dst is unusable and must be released here.
  if (!ok) {
    free(dst);
    return -6;
  }

  // すべて成功した時点でのみ、完成した画像を呼び出し側へ公開します。 / Publish the resized image to the caller only after all operations succeed.
  out->w = new_w;
  out->h = new_h;
  out->channels = src->channels;
  out->pixels = dst;

  return 0;
}

int img_save_png(const char* path, const Image* img) {
  // path: 保存先ファイルパスです。 / path: destination file path.
  // img: PNGとしてシリアライズする元画像バッファです。 / img: source image buffer to serialize as PNG.
  if (!path || !img || !img->pixels || img->w <= 0 || img->h <= 0 || img->channels <= 0) {
    return -1;
  }

  // stride: 1行ぶんのバイト数です。 / stride: number of bytes in one scanline.
  // stb_image_write はこの値を使って元バッファを行単位で走査します。 / stb_image_write uses this to walk row by row through the source buffer.
  const int stride = img->w * img->channels;
  if (!stbi_write_png(path, img->w, img->h, img->channels, img->pixels, stride)) {
    return -2;
  }

  return 0;
}

int img_save_jpg(const char* path, const Image* img, int quality) {
  if (!path || !img || !img->pixels || img->w <= 0 || img->h <= 0 || img->channels <= 0) {
    return -1;
  }

  uint8_t* rgb = img_copy_rgb(img);
  if (!rgb) {
    return -2;
  }

  const int ok = stbi_write_jpg(path, img->w, img->h, 3, rgb, img_normalize_quality(quality));
  free(rgb);

  if (!ok) {
    return -3;
  }

  return 0;
}

int img_save_pdf(const char* path, const Image* img, int quality) {
  if (!path || !img || !img->pixels || img->w <= 0 || img->h <= 0 || img->channels <= 0) {
    return -1;
  }

  uint8_t* rgb = img_copy_rgb(img);
  if (!rgb) {
    return -2;
  }

  ImgByteBuffer jpeg = {0};
  if (!stbi_write_jpg_to_func(
          img_stbi_write_to_buffer,
          &jpeg,
          img->w,
          img->h,
          3,
          rgb,
          img_normalize_quality(quality))) {
    free(rgb);
    free(jpeg.data);
    return -3;
  }
  free(rgb);

  char content[256];
  const int content_length = snprintf(
      content,
      sizeof(content),
      "q\n%d 0 0 %d 0 0 cm\n/Im0 Do\nQ\n",
      img->w,
      img->h);
  if (content_length <= 0 || content_length >= (int)sizeof(content)) {
    free(jpeg.data);
    return -4;
  }

  FILE* file = fopen(path, "wb");
  if (!file) {
    free(jpeg.data);
    return -5;
  }

  long offsets[6] = {0};
  long offset = 0;
  int ok = 1;

  ok = ok && img_pdf_write_text(file, &offset, "%PDF-1.4\n");

  offsets[0] = offset;
  ok = ok && img_pdf_write_text(file, &offset, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

  offsets[1] = offset;
  {
    char buffer[128];
    const int length = snprintf(buffer, sizeof(buffer), "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
  }

  offsets[2] = offset;
  {
    char buffer[512];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /ProcSet [/PDF /ImageC] /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\nendobj\n",
        img->w,
        img->h);
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
  }

  offsets[3] = offset;
  {
    char buffer[256];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "4 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %zu >>\nstream\n",
        img->w,
        img->h,
        jpeg.size);
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
    ok = ok && img_pdf_write_bytes(file, &offset, jpeg.data, jpeg.size);
    ok = ok && img_pdf_write_text(file, &offset, "\nendstream\nendobj\n");
  }

  offsets[4] = offset;
  {
    char buffer[256];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "5 0 obj\n<< /Length %d >>\nstream\n%sendstream\nendobj\n",
        content_length,
        content);
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
  }

  const long xref_offset = offset;
  ok = ok && img_pdf_write_text(file, &offset, "xref\n0 6\n");
  ok = ok && img_pdf_write_text(file, &offset, "0000000000 65535 f \n");

  for (size_t i = 0; i < 5u; ++i) {
    char buffer[32];
    const int length = snprintf(buffer, sizeof(buffer), "%010ld 00000 n \n", offsets[i]);
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
  }

  {
    char buffer[256];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
        xref_offset);
    ok = ok && (length > 0) && img_pdf_write_bytes(file, &offset, buffer, (size_t)length);
  }

  fclose(file);
  free(jpeg.data);

  if (!ok) {
    return -6;
  }

  return 0;
}

int img_save_with_format(const char* path, const Image* img, ImageFileFormat format, int quality) {
  switch (format) {
    case IMG_FORMAT_PNG:
      return img_save_png(path, img);
    case IMG_FORMAT_JPG:
      return img_save_jpg(path, img, quality);
    case IMG_FORMAT_PDF:
      return img_save_pdf(path, img, quality);
    default:
      return -1;
  }
}

void img_free(Image* img) {
  // img: 内部ヒープバッファを解放したい Image オブジェクトです。 / img: Image object whose internal heap buffer should be released.
  if (!img) {
    return;
  }

  // stbi_image_free は stbi_load が返したバッファに対して有効です。 / stbi_image_free is valid for buffers returned by stbi_load.
  // このプロジェクトでは stb が標準Cアロケータを使う前提のため、リサイズ後バッファにも同じ解放関数を使っています。 / In this project it is also used for resized buffers because stb defaults to the standard C allocator pair.
  if (img->pixels) {
    stbi_image_free(img->pixels);
  }

  // 再利用時や二重解放防止のため、メタデータも初期状態へ戻します。 / Reset metadata so the struct is safe to reuse and double-free resistant.
  img_reset(img);
}
