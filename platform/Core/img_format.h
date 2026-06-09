#ifndef IMG_FORMAT_H
#define IMG_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IMG_NUM_DEFAULT_PALS 3

#pragma pack(push, 2)
struct ImgLibHeaderDisk {
    unsigned short imgcnt;
    unsigned short palcnt;
    unsigned int   oset;
    unsigned short version;
    unsigned short seqcnt;
    unsigned short scrcnt;
    unsigned short damcnt;
    unsigned short temp;
    unsigned char  bufscr[4];
    unsigned short spare1;
    unsigned short spare2;
    unsigned short spare3;
};

struct ImgImageDisk {
    char           name[16];
    unsigned short flags;
    unsigned short anix;
    unsigned short aniy;
    unsigned short w;
    unsigned short h;
    unsigned short palnum;
    unsigned int   oset;
    unsigned int   data;
    unsigned short lib;
    unsigned short anix2;
    unsigned short aniy2;
    unsigned short aniz2;
    unsigned short frm;
    unsigned short pttblnum;
    unsigned short opals;
};

struct ImgPaletteDisk {
    char           name[10];
    unsigned char  flags;
    unsigned char  bitspix;
    unsigned short numc;
    unsigned int   oset;
    unsigned short data;
    unsigned short lib;
    unsigned char  colind;
    unsigned char  cmap;
    unsigned short spare;
};
#pragma pack(pop)

static_assert(sizeof(ImgImageDisk) == 50, "Unexpected MK2 IMG image header size");
static_assert(sizeof(ImgPaletteDisk) == 26, "Unexpected MK2 IMG palette header size");

long img_file_size_for_import(FILE *f);
int img_s16(unsigned short v);
void img_basename_no_ext_upper(const char *path, char *out, size_t outsz);
void img_raw_name_to_upper(const char *raw, int raw_len, const char *fallback,
                           char *out, size_t outsz);
uint32_t img_pal_word_to_argb_opaque(unsigned short c);
uint32_t img_pal_word_to_argb(unsigned short c, int index);
int img_decode_pixels(FILE *f, long file_sz, const ImgImageDisk *id,
                      int w, int h, unsigned char *dst,
                      const unsigned char *pix_map,
                      int *visible_zero_remaps);

#endif
