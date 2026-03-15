#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jpeglib.h"
#include <png.h>
#include <emscripten.h>

// Headers do NBIS
#include "wsq.h"
#include "jpegb.h"

// VARIÁVEL GLOBAL EXIGIDA PELO NBIS
int debug = 0;

// --- FUNÇÕES DE IO BLINDADAS (MATADORAS DE ENDIANNESS) ---
// Essas funções substituem as originais do NIST e garantem leitura Big-Endian correta

int getc_byte(unsigned char *b, unsigned char **ptr, unsigned char *end) {
    if (*ptr >= end) return -1;
    *b = *(*ptr)++;
    return 0;
}

int putc_byte(const unsigned char b, unsigned char *odata, const int oalloc, int *olen) {
    if (*olen >= oalloc) return -1;
    odata[(*olen)++] = b;
    return 0;
}

int getc_ushort(unsigned short *s, unsigned char **ptr, unsigned char *end) {
    if (*ptr + 2 > end) return -1;
    unsigned char b1 = *(*ptr)++;
    unsigned char b2 = *(*ptr)++;
    *s = (unsigned short)((b1 << 8) | b2);
    return 0;
}

int putc_ushort(unsigned short s, unsigned char *odata, const int oalloc, int *olen) {
    if (*olen + 2 > oalloc) return -1;
    odata[(*olen)++] = (unsigned char)((s >> 8) & 0xFF);
    odata[(*olen)++] = (unsigned char)(s & 0xFF);
    return 0;
}

int getc_uint(unsigned int *i, unsigned char **ptr, unsigned char *end) {
    if (*ptr + 4 > end) return -1;
    unsigned char b1 = *(*ptr)++;
    unsigned char b2 = *(*ptr)++;
    unsigned char b3 = *(*ptr)++;
    unsigned char b4 = *(*ptr)++;
    *i = (unsigned int)((b1 << 24) | (b2 << 16) | (b3 << 8) | b4);
    return 0;
}

int putc_uint(unsigned int i, unsigned char *odata, const int oalloc, int *olen) {
    if (*olen + 4 > oalloc) return -1;
    odata[(*olen)++] = (unsigned char)((i >> 24) & 0xFF);
    odata[(*olen)++] = (unsigned char)((i >> 16) & 0xFF);
    odata[(*olen)++] = (unsigned char)((i >> 8) & 0xFF);
    odata[(*olen)++] = (unsigned char)(i & 0xFF);
    return 0;
}

int getc_marker_wsq(unsigned short *m, const int type, unsigned char **ptr, unsigned char *end) {
    if (getc_ushort(m, ptr, end) != 0) return -1;
    // SOI_WSQ = 0xFFA0 (decimal 65440)
    if (type == 1 && *m != 0xFFA0) return -88; 
    return 0;
}

// --- LÓGICA DE CONVERSÃO ---

typedef struct {
    unsigned char *data;
    int size;
} MemoryBlock;

// Decode JPG usando a libjpeg interna do NBIS (v90)
unsigned char* decode_jpg_to_gray(MemoryBlock input, int *w, int *h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_membuf_src(&cinfo, input.data, input.size);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_GRAYSCALE;
    jpeg_start_decompress(&cinfo);
    *w = cinfo.output_width;
    *h = cinfo.output_height;
    unsigned char *out = (unsigned char *)malloc((*w) * (*h));
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *rowptr = out + (cinfo.output_scanline * (*w));
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return out;
}

// PNG Decode (Simples)
unsigned char* decode_png_to_gray(MemoryBlock input, int *w, int *h) {
    FILE *infile = fmemopen(input.data, input.size, "rb");
    if (!infile) return NULL;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) { fclose(infile); return NULL; }
    png_init_io(png, infile);
    png_read_info(png, info);
    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    png_set_rgb_to_gray_fixed(png, 1, -1, -1);
    png_set_strip_alpha(png);
    png_read_update_info(png, info);
    unsigned char *buffer = (unsigned char *)malloc((*w) * (*h));
    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * (*h));
    for(int y = 0; y < *h; y++) row_pointers[y] = buffer + (y * (*w));
    png_read_image(png, row_pointers);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(infile);
    return buffer;
}

MemoryBlock img_to_wsq(MemoryBlock input, int is_png) {
    int w, h;
    unsigned char *gray = is_png ? decode_png_to_gray(input, &w, &h) : decode_jpg_to_gray(input, &w, &h);
    MemoryBlock out = {NULL, 0};
    if (!gray) return out;
    int out_size = 0;
    int ret = wsq_encode_mem(&out.data, &out_size, 0.75f, gray, w, h, 8, 500, NULL);
    out.size = out_size;
    free(gray);
    if (ret != 0) { if(out.data) free(out.data); out.data = NULL; out.size = 0; }
    return out;
}

MemoryBlock wsq_to_img(MemoryBlock input, int to_png) {
    unsigned char *raw = NULL;
    int w, h, d, ppi, lossy;
    MemoryBlock out = {NULL, 0};
    int ret = wsq_decode_mem(&raw, &w, &h, &d, &ppi, &lossy, input.data, input.size);
    if (ret != 0 || !raw) return out;
    int out_size = 0;
    // Por enquanto convertendo sempre para JPEG
    ret = jpegb_encode_mem(&out.data, &out_size, 90, raw, w, h, 8, 500, NULL);
    out.size = out_size;
    free(raw);
    return out;
}

EMSCRIPTEN_KEEPALIVE
unsigned char* converter_para_wsq(unsigned char* buffer_entrada, int tamanho_entrada, int eh_png, int* tamanho_saida) {
    MemoryBlock input = {buffer_entrada, tamanho_entrada};
    MemoryBlock output = img_to_wsq(input, eh_png);
    *tamanho_saida = output.size;
    return output.data;
}

EMSCRIPTEN_KEEPALIVE
unsigned char* converter_de_wsq(unsigned char* buffer_entrada, int tamanho_entrada, int para_png, int* tamanho_saida) {
    MemoryBlock input = {buffer_entrada, tamanho_entrada};
    MemoryBlock output = wsq_to_img(input, para_png);
    *tamanho_saida = output.size;
    return output.data;
}

EMSCRIPTEN_KEEPALIVE
void liberar_memoria(unsigned char* buffer) {
    if (buffer) free(buffer);
}