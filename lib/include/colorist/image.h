// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2018.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#ifndef COLORIST_IMAGE_H
#define COLORIST_IMAGE_H

#include "colorist/types.h"

struct clContext;
struct clProfile;

typedef struct clImage
{
    int width;
    int height;
    int depth;
    int size;
    uint8_t * pixels;
    struct clProfile * profile;
} clImage;

clImage * clImageCreate(struct clContext * C, int width, int height, int depth, struct clProfile * profile);
void clImageResize(struct clContext * C, clImage * image, int width, int height, int depth);
void clImageChangeDepth(struct clContext * C, clImage * image, int depth);
void clImageSetPixel(struct clContext * C, clImage * image, int x, int y, int r, int g, int b, int a);
void clImageDebugDump(struct clContext * C, clImage * image, int x, int y, int w, int h, int extraIndent);
void clImageDestroy(struct clContext * C, clImage * image);

clImage * clImageReadPNG(struct clContext * C, const char * filename);
clBool clImageWritePNG(struct clContext * C, clImage * image, const char * filename);

clImage * clImageReadJP2(struct clContext * C, const char * filename);
clBool clImageWriteJP2(struct clContext * C, clImage * image, const char * filename, int quality, int rate);

clImage * clImageReadJPG(struct clContext * C, const char * filename);
clBool clImageWriteJPG(struct clContext * C, clImage * image, const char * filename, int quality);

#endif // ifndef COLORIST_IMAGE_H
