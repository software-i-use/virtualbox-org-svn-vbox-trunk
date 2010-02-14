/* Generate the VGABIOS VBE Tables */
#include <stdlib.h>
#include <stdio.h>

#define VBE_DISPI_TOTAL_VIDEO_MEMORY_MB 8

typedef struct {
    int width;
    int height;
    int depth;
    int mode;
} ModeInfo;

ModeInfo modes[] = {
    /* standard VESA modes */
{ 640, 400, 8                          , 0x100},
{ 640, 480, 8                          , 0x101},
{ 800, 600, 4                          , 0x102},
{ 800, 600, 8                          , 0x103},
{ 1024, 768, 4                         , 0x104},
{ 1024, 768, 8                         , 0x105},
{ 1280, 1024, 4                        , 0x106},
{ 1280, 1024, 8                        , 0x107},
{ 320, 200, 15                       , 0x10D},
{ 320, 200, 16                        , 0x10E},
{ 320, 200, 24                        , 0x10F},
{ 640, 480, 15                       , 0x110},
{ 640, 480, 16                        , 0x111},
{ 640, 480, 24                        , 0x112},
{ 800, 600, 15                       , 0x113},
{ 800, 600, 16                        , 0x114},
{ 800, 600, 24                        , 0x115},
{ 1024, 768, 15                      , 0x116},
{ 1024, 768, 16                       , 0x117},
{ 1024, 768, 24                       , 0x118},
{ 1280, 1024, 15                     , 0x119},
{ 1280, 1024, 16                      , 0x11A},
{ 1280, 1024, 24                      , 0x11B},
#if 0 /* Ubuntu fails with this */
{ 1600, 1200, 8                        , 0x11C},
{ 1600, 1200, 15                     , 0x11D},
{ 1600, 1200, 16                      , 0x11E},
{ 1600, 1200, 24                      , 0x11F},
#endif

      /* BOCHS/PLE, 86 'own' mode numbers */
{ 320, 200, 32                        , 0x140},
{ 640, 400, 32                        , 0x141},
{ 640, 480, 32                        , 0x142},
{ 800, 600, 32                        , 0x143},
{ 1024, 768, 32                       , 0x144},
{ 1280, 1024, 32                      , 0x145},
{ 320, 200, 8                           , 0x146},
{ 1600, 1200, 32                      , 0x147},
{ 1152, 864, 8                      , 0x148},
{ 1152, 864, 15                      , 0x149},
{ 1152, 864, 16                      , 0x14a},
{ 1152, 864, 24                      , 0x14b},
{ 1152, 864, 32                      , 0x14c},
{ 0, },
};

int main(int argc, char **argv)
{
  const ModeInfo *pm;
  int pages, pitch;
  int r_size, r_pos, g_size, g_pos, b_size, b_pos, a_size, a_pos;
  const char *str;
  long vram_size = VBE_DISPI_TOTAL_VIDEO_MEMORY_MB * 1024 * 1024;

  printf("/* THIS FILE IS AUTOMATICALLY GENERATED - DO NOT EDIT */\n\n");
  printf("#define VBE_DISPI_TOTAL_VIDEO_MEMORY_MB %d\n\n", VBE_DISPI_TOTAL_VIDEO_MEMORY_MB);
  printf("static ModeInfoListItem mode_info_list[]=\n");
  printf("{\n");
  for (pm = modes; pm->mode != 0; pm++) {
    if (pm->depth == 4)
      pitch = (pm->width + 7) / 8;
    else
      pitch = pm->width * ((pm->depth + 7) / 8);
    pages = vram_size / (pm->height * pitch);
    if (pages > 0) {
      printf("{ 0x%04x, /* %dx%dx%d */\n",
             pm->mode, pm->width, pm->height, pm->depth);
      if (pm->depth == 4)
        printf("{ /*Bit16u ModeAttributes*/ %s,\n",
               "VBE_MODE_ATTRIBUTE_SUPPORTED | "
               "VBE_MODE_ATTRIBUTE_EXTENDED_INFORMATION_AVAILABLE | "
               "VBE_MODE_ATTRIBUTE_COLOR_MODE | "
               "VBE_MODE_ATTRIBUTE_TTY_BIOS_SUPPORT | "
               "VBE_MODE_ATTRIBUTE_GRAPHICS_MODE");
      else
        printf("{ /*Bit16u ModeAttributes*/ %s,\n",
               "VBE_MODE_ATTRIBUTE_SUPPORTED | "
               "VBE_MODE_ATTRIBUTE_EXTENDED_INFORMATION_AVAILABLE | "
               "VBE_MODE_ATTRIBUTE_COLOR_MODE | "
               "VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE | "
               "VBE_MODE_ATTRIBUTE_GRAPHICS_MODE");
      printf("/*Bit8u  WinAAttributes*/ %s,\n",
             "VBE_WINDOW_ATTRIBUTE_RELOCATABLE | "
             "VBE_WINDOW_ATTRIBUTE_READABLE | "
             "VBE_WINDOW_ATTRIBUTE_WRITEABLE");

      printf("/*Bit8u  WinBAttributes*/ %d,\n", 0);

      printf("/*Bit16u WinGranularity*/ %s,\n", "VBE_DISPI_BANK_SIZE_KB");

      printf("/*Bit16u WinSize*/ %s,\n", "VBE_DISPI_BANK_SIZE_KB");

      printf("/*Bit16u WinASegment*/ %s,\n", "VGAMEM_GRAPH");

      printf("/*Bit16u WinBSegment*/ 0x%04x,\n", 0);

      printf("/*Bit32u WinFuncPtr*/ %d,\n", 0);

      printf("/*Bit16u BytesPerScanLine*/ %d,\n", pitch);

      /* Mandatory information for VBE 1.2 and above */
      printf("/*Bit16u XResolution*/ %d,\n", pm->width);
      printf("/*Bit16u YResolution*/ %d,\n", pm->height);
      printf("/*Bit8u  XCharSize*/ %d,\n", 8);
      printf("/*Bit8u  YCharSize*/ %d,\n", 16);
      if (pm->depth == 4) {
        printf("/*Bit8u  NumberOfPlanes*/ %d,\n", 4);
      } else {
        printf("/*Bit8u  NumberOfPlanes*/ %d,\n", 1);
      }
      printf("/*Bit8u  BitsPerPixel*/ %d,\n", pm->depth);
      printf("/*Bit8u  NumberOfBanks*/ %d,\n", 1);  /* This would only be non-zero for CGA/HGC modes! */

      if (pm->depth == 4)
        str = "VBE_MEMORYMODEL_PLANAR";
      else if (pm->depth == 8)
        str = "VBE_MEMORYMODEL_PACKED_PIXEL";
      else
        str = "VBE_MEMORYMODEL_DIRECT_COLOR";
      printf("/*Bit8u  MemoryModel*/ %s,\n", str);
      printf("/*Bit8u  BankSize*/ %d,\n", 0);
      if (pm->depth == 4)
        printf("/*Bit8u  NumberOfImagePages*/ %d,\n", (pages / 4) - 1);
      else
        printf("/*Bit8u  NumberOfImagePages*/ %d,\n", pages - 1);
      printf("/*Bit8u  Reserved_page*/ %d,\n", 0);

      /* Direct Color fields (required for direct/6 and YUV/7 memory models) */
      switch(pm->depth) {
        case 15:
          r_size = 5;
          r_pos = 10;
          g_size = 5;
          g_pos = 5;
          b_size = 5;
          b_pos = 0;
          a_size = 1;
          a_pos = 15;
          break;
        case 16:
          r_size = 5;
          r_pos = 11;
          g_size = 6;
          g_pos = 5;
          b_size = 5;
          b_pos = 0;
          a_size = 0;
          a_pos = 0;
          break;
        case 24:
          r_size = 8;
          r_pos = 16;
          g_size = 8;
          g_pos = 8;
          b_size = 8;
          b_pos = 0;
          a_size = 0;
          a_pos = 0;
          break;
        case 32:
          r_size = 8;
          r_pos = 16;
          g_size = 8;
          g_pos = 8;
          b_size = 8;
          b_pos = 0;
          a_size = 8;
          a_pos = 24;
          break;
        default:
          r_size = 0;
          r_pos = 0;
          g_size = 0;
          g_pos = 0;
          b_size = 0;
          b_pos = 0;
          a_size = 0;
          a_pos = 0;
          break;
      }

      printf("/*Bit8u  RedMaskSize*/ %d,\n", r_size);
      printf("/*Bit8u  RedFieldPosition*/ %d,\n", r_pos);
      printf("/*Bit8u  GreenMaskSize*/ %d,\n", g_size);
      printf("/*Bit8u  GreenFieldPosition*/ %d,\n", g_pos);
      printf("/*Bit8u  BlueMaskSize*/ %d,\n", b_size);
      printf("/*Bit8u  BlueFieldPosition*/ %d,\n", b_pos);
      printf("/*Bit8u  RsvdMaskSize*/ %d,\n", a_size);
      printf("/*Bit8u  RsvdFieldPosition*/ %d,\n", a_pos);
      if (pm->depth == 32)
        printf("/*Bit8u  DirectColorModeInfo*/ %s,\n",
               "VBE_DIRECTCOLOR_RESERVED_BITS_AVAILABLE");
      else
        printf("/*Bit8u  DirectColorModeInfo*/ %s,\n", "0");

      /* Mandatory information for VBE 2.0 and above */
      if (pm->depth > 4)
        printf("/*Bit32u PhysBasePtr*/ %s,\n",
               "VBE_DISPI_LFB_PHYSICAL_ADDRESS");
      else
        printf("/*Bit32u PhysBasePtr*/ %s,\n", "0");
      printf("/*Bit32u OffScreenMemOffset*/ %d,\n", 0);
      printf("/*Bit16u OffScreenMemSize*/ %d,\n", 0);
      /* Mandatory information for VBE 3.0 and above */
      printf("/*Bit16u LinBytesPerScanLine*/ %d,\n", pitch);
      printf("/*Bit8u  BnkNumberOfPages*/ %d,\n", 0);
      printf("/*Bit8u  LinNumberOfPages*/ %d,\n", 0);
      printf("/*Bit8u  LinRedMaskSize*/ %d,\n", r_size);
      printf("/*Bit8u  LinRedFieldPosition*/ %d,\n", r_pos);
      printf("/*Bit8u  LinGreenMaskSize*/ %d,\n", g_size);
      printf("/*Bit8u  LinGreenFieldPosition*/ %d,\n", g_pos);
      printf("/*Bit8u  LinBlueMaskSize*/ %d,\n", b_size);
      printf("/*Bit8u  LinBlueFieldPosition*/ %d,\n", b_pos);
      printf("/*Bit8u  LinRsvdMaskSize*/ %d,\n", a_size);
      printf("/*Bit8u  LinRsvdFieldPosition*/ %d,\n", a_pos);
      printf("/*Bit32u MaxPixelClock*/ %d,\n", 0);
      printf("} },\n");
    }
  }
#ifndef VBOX    /* We'll add the terminator ourselves, thank you. */
  printf("{ VBE_VESA_MODE_END_OF_LIST,\n");
  printf("{ 0,\n");
  printf("} },\n");
#endif
  printf("};\n");
  return 0;
}
