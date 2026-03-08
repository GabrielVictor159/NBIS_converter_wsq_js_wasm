#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <png.h>
#include <emscripten.h>

// Headers do NBIS
#include "wsq.h"
#include "jpegb.h"

int debug = 0;

typedef struct {
    unsigned char *data;
    size_t size;
} MemoryBlock;

// --- JPEG DECODE ---
unsigned char* decode_jpg_to_gray(MemoryBlock input, int *w, int *h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *infile = fmemopen(input.data, input.size, "rb");
    if (!infile) return NULL;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_GRAYSCALE;
    jpeg_start_decompress(&cinfo);

    *w = cinfo.output_width;
    *h = cinfo.output_height;

    unsigned char *out = malloc((*w) * (*h));
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *rowptr = out + (cinfo.output_scanline * (*w));
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    return out;
}

// --- PNG DECODE ---
unsigned char* decode_png_to_gray(MemoryBlock input, int *w, int *h) {
    FILE *infile = fmemopen(input.data, input.size, "rb");
    if (!infile) return NULL;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(infile);
        return NULL;
    }

    png_init_io(png, infile);
    png_read_info(png, info);

    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);

    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    if(bit_depth == 16) png_set_strip_16(png);
    if(color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if(png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_RGBA) png_set_rgb_to_gray_fixed(png, 1, -1, -1);
    if(color_type == PNG_COLOR_TYPE_RGBA || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);

    unsigned char *buffer = malloc((*w) * (*h));
    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * (*h));
    for(int y = 0; y < *h; y++) row_pointers[y] = buffer + (y * (*w));

    png_read_image(png, row_pointers);

    png_destroy_read_struct(&png, &info, NULL);
    free(row_pointers);
    fclose(infile);
    return buffer;
}

// --- PNG ENCODE ---
MemoryBlock encode_gray_to_png(unsigned char *raw, int w, int h) {
    MemoryBlock out = {NULL, 0};
    FILE *outfile = open_memstream((char **)&out.data, &out.size);
    if (!outfile) return out;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        fclose(outfile);
        return out;
    }

    png_init_io(png, outfile);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * h);
    for(int y = 0; y < h; y++) row_pointers[y] = raw + (y * w);

    png_write_image(png, row_pointers);
    png_write_end(png, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png, &info);
    fclose(outfile);
    return out;
}

// --- CORE FUNCTIONS ---

MemoryBlock img_to_wsq(MemoryBlock input, int is_png) {
    int w, h;
    unsigned char *gray = is_png ? decode_png_to_gray(input, &w, &h) : decode_jpg_to_gray(input, &w, &h);
    MemoryBlock out = {NULL, 0};
    if (!gray) return out;

    int w_par = (w % 2 == 0) ? w : w - 1;
    // 0.75 é o bitrate padrão para compressão WSQ (15:1)
    int ret = wsq_encode_mem(&out.data, (int*)&out.size, 0.75, gray, w_par, h, 1, 500, NULL);
    
    free(gray);
    if (ret != 0) { 
        if(out.data) free(out.data); 
        out.data = NULL; 
        out.size = 0;
    }
    return out;
}

MemoryBlock wsq_to_img(MemoryBlock input, int to_png) {
    unsigned char *raw = NULL;
    int w, h, d, ppi, lossy;
    MemoryBlock out = {NULL, 0};

    FILE *infile = fmemopen(input.data, input.size, "rb");
    if (!infile) return out;

    if (wsq_decode_file(&raw, &w, &h, &d, &ppi, &lossy, infile) != 0) {
        fclose(infile);
        return out;
    }
    fclose(infile);

    if (to_png) {
        out = encode_gray_to_png(raw, w, h);
    } else {
        // Encode para JPEG usando o NBIS
        jpegb_encode_mem(&out.data, (int*)&out.size, 90, raw, w, h, d, ppi, NULL);
    }

    free(raw);
    return out;
}

// --- EXPORTS ---

EMSCRIPTEN_KEEPALIVE
unsigned char* converter_para_wsq(unsigned char* buffer_entrada, int tamanho_entrada, int eh_png, int* tamanho_saida) {
    MemoryBlock input = {buffer_entrada, (size_t)tamanho_entrada};
    MemoryBlock output = img_to_wsq(input, eh_png);
    *tamanho_saida = (int)output.size;
    return output.data;
}

EMSCRIPTEN_KEEPALIVE
unsigned char* converter_de_wsq(unsigned char* buffer_entrada, int tamanho_entrada, int para_png, int* tamanho_saida) {
    MemoryBlock input = {buffer_entrada, (size_t)tamanho_entrada};
    MemoryBlock output = wsq_to_img(input, para_png);
    *tamanho_saida = (int)output.size;
    return output.data;
}

EMSCRIPTEN_KEEPALIVE
void liberar_memoria(unsigned char* buffer) {
    if (buffer) free(buffer);
}