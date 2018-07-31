// ---------------------------------------------------------------------------
//                         Copyright Joe Drago 2018.
//         Distributed under the Boost Software License, Version 1.0.
//            (See accompanying file LICENSE_1_0.txt or copy at
//                  http://www.boost.org/LICENSE_1_0.txt)
// ---------------------------------------------------------------------------

#include "colorist/image.h"

#include "colorist/context.h"
#include "colorist/image.h"
#include "colorist/pixelmath.h"
#include "colorist/profile.h"
#include "colorist/task.h"

#define FAIL() { result = clFalse; goto convertCleanup; }

void doMultithreadedTransform(clContext * C, int taskCount, cmsHTRANSFORM transform, uint8_t * srcPixels, int srcPixelBytes, uint8_t * dstPixels, int dstPixelBytes, int pixelCount);

clImage * clImageConvert(struct clContext * C, clImage * srcImage, struct clConversionParams * params)
{
    clBool result = clTrue;

    Timer t;
    int returnCode = 0;

    float srcGamma = 0.0f;
    int srcLuminance = 0;

    clProfilePrimaries dstPrimaries;
    float dstGamma = 0.0f;
    int dstLuminance = 0;
    int bestDepth;
    int dstDepth = 0;
    clProfile * dstProfile = NULL;
    clImage * dstImage = NULL;

    // if true, don't bother with an intermediate array of floats, just use LittleCMS
    clBool convertDirectly = clTrue;

    // Variables used in luminance scaling
    clProfile * dstLinear = NULL;
    int linearPixelsCount = 0;
    float * linearPixels = NULL;
    float luminanceScale = 0.0f;
    clBool tonemap = clFalse;

    // Variables used for resizing
    int dstWidth = 0;
    int dstHeight = 0;
    clBool resizing = clFalse;

    // Hald CLUT
    clImage * haldImage = NULL;
    int haldDims = 0;

    // Parse source image and args for early pipeline decisions
    {
        clProfileCurve curve;
        clProfileQuery(C, srcImage->profile, &dstPrimaries, &curve, &srcLuminance);

        // Primaries
        if (params->primaries[0] > 0.0f) {
            dstPrimaries.red[0] = params->primaries[0];
            dstPrimaries.red[1] = params->primaries[1];
            dstPrimaries.green[0] = params->primaries[2];
            dstPrimaries.green[1] = params->primaries[3];
            dstPrimaries.blue[0] = params->primaries[4];
            dstPrimaries.blue[1] = params->primaries[5];
            dstPrimaries.white[0] = params->primaries[6];
            dstPrimaries.white[1] = params->primaries[7];
        }

        // Luminance
        srcLuminance = (srcLuminance != 0) ? srcLuminance : COLORIST_DEFAULT_LUMINANCE;
        if (params->luminance < 0) {
            dstLuminance = srcLuminance;
        } else if (params->luminance != 0) {
            dstLuminance = params->luminance;
        }

        // Gamma
        srcGamma = curve.gamma;
        if ((curve.type != CL_PCT_GAMMA) && (srcGamma > 0.0f)) {
            clContextLog(C, "info", 0, "Estimated source gamma: %g", srcGamma);
        }
        if (params->gamma < 0.0f) {
            dstGamma = srcGamma;
        } else if (params->gamma > 0.0f) {
            dstGamma = params->gamma;
        }

        // Depth
        dstDepth = params->bpp;
        if (dstDepth == 0) {
            dstDepth = srcImage->depth;
        }
        bestDepth = clFormatBestDepth(C, params->format, dstDepth);
        if (dstDepth != bestDepth) {
            clContextLog(C, "validate", 0, "Overriding output depth %d-bit -> %d-bit (format limitations)", dstDepth, bestDepth);
            dstDepth = bestDepth;
        }

        if (!params->autoGrade) {
            if (dstGamma == 0.0f) {
                dstGamma = srcGamma;
            }
            if (dstLuminance == 0) {
                dstLuminance = srcLuminance;
            }
        }

        if ((params->resizeW > 0) || (params->resizeH > 0)) {
            if (params->resizeW <= 0) {
                dstWidth = (int)(((float)srcImage->width / (float)srcImage->height) * params->resizeH);
                dstHeight = params->resizeH;
            } else if (params->resizeH <= 0) {
                dstWidth = params->resizeW;
                dstHeight = (int)(((float)srcImage->height / (float)srcImage->width) * params->resizeW);
            } else {
                dstWidth = params->resizeW;
                dstHeight = params->resizeH;
            }
            if (dstWidth <= 0)
                dstWidth = 1;
            if (dstHeight <= 0)
                dstHeight = 1;
        } else {
            dstWidth = srcImage->width;
            dstHeight = srcImage->height;
        }
        resizing = ((dstWidth != srcImage->width) || (dstHeight != srcImage->height)) ? clTrue : clFalse;
    }

    // Load output profile override, if any
    if (params->iccOverrideOut) {
        dstProfile = clProfileRead(C, params->iccOverrideOut);
        if (!dstProfile) {
            clContextLogError(C, "Invalid destination profile override: %s", params->iccOverrideOut);
            FAIL();
        }

        // Pull dstLuminance out of the overridden profile, if present
        clProfileQuery(C, dstProfile, NULL, NULL, &dstLuminance);
        if (dstLuminance == 0) {
            dstLuminance = srcLuminance;
        }

        clContextLog(C, "profile", 1, "Overriding dst profile with file: %s", params->iccOverrideOut);
    }

    // Decide whether or not to create intermediate float array or convert directly with Little CMS
    if (srcLuminance != dstLuminance) {
        // Luminance scaling is involved, floats needed
        convertDirectly = clFalse;
    }
    if (resizing) {
        // resizing occurs with floats
        convertDirectly = clFalse;
    }
    if (((srcImage->depth != 8) && (srcImage->depth != 16)) || ((dstDepth != 8) && (dstDepth != 16))) {
        // LittleCMS can only directly convert from/to 8 or 16 bit formats
        convertDirectly = clFalse;
    }
    if (params->hald) {
        haldImage = clContextRead(C, params->hald, NULL, NULL);
        if (!haldImage) {
            clContextLogError(C, "Can't read Hald CLUT: %s", params->hald);
            FAIL();
        }
        if (haldImage->width != haldImage->height) {
            clContextLogError(C, "Hald CLUT isn't square [%dx%d]: %s", haldImage->width, haldImage->height, params->hald);
            FAIL();
        }

        // Find haldDims
        {
            int i;
            for (i = 0; i < 32; ++i) {
                if ((i * i * i) == haldImage->width) {
                    haldDims = i * i;
                    break;
                }
            }

            if (haldDims == 0) {
                clContextLogError(C, "Hald CLUT dimensions aren't cubic [%dx%d]: %s", haldImage->width, haldImage->height, params->hald);
                FAIL();
            }

            clContextLog(C, "hald", 0, "Loaded %dx%dx%d Hald CLUT: %s", haldDims, haldDims, haldDims, params->hald);
        }

        // Using a Hald CLUT, we need some source floats
        convertDirectly = clFalse;
    }

    // Create intermediate 1.0 gamma float32 pixel array if we're going to need it later.
    if (!convertDirectly) {
        cmsHTRANSFORM toLinear;
        float * srcFloats; // original values in floating point, manually created to avoid cms eval'ing on a 16-bit basis for floats (yuck)

        clProfileCurve gamma1;
        gamma1.type = CL_PCT_GAMMA;
        gamma1.gamma = 1.0f;
        dstLinear = clProfileCreate(C, &dstPrimaries, &gamma1, 0, NULL);

        linearPixelsCount = srcImage->width * srcImage->height;
        linearPixels = clAllocate(4 * sizeof(float) * linearPixelsCount);
        toLinear = cmsCreateTransformTHR(C->lcms, srcImage->profile->handle, TYPE_RGBA_FLT, dstLinear->handle, TYPE_RGBA_FLT, INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE);

        clContextLog(C, "convert", 0, "Calculating linear pixels...");
        timerStart(&t);
        srcFloats = clAllocate(4 * sizeof(float) * linearPixelsCount);
        clPixelMathUNormToFloat(C, srcImage->pixels, srcImage->depth, srcFloats, linearPixelsCount);
        doMultithreadedTransform(C, params->jobs, toLinear, (uint8_t *)srcFloats, 4 * sizeof(float), (uint8_t *)linearPixels, 4 * sizeof(float), linearPixelsCount);
        cmsDeleteTransform(toLinear);
        clFree(srcFloats);
        clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
    }

    if (params->autoGrade) {
        clContextLog(C, "grading", 0, "Color grading...");
        timerStart(&t);
        clPixelMathColorGrade(C, params->jobs, linearPixels, linearPixelsCount, srcLuminance, dstDepth, &dstLuminance, &dstGamma, C->verbose);
        clContextLog(C, "grading", 0, "Using maxLum: %d, gamma: %g", dstLuminance, dstGamma);
        clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
    }

    // If we survive arg parsing and autoGrade mode and still don't have a reasonable luminance and gamma, bail out.
    if ((dstLuminance == 0) || (dstGamma == 0.0f)) {
        clContextLogError(C, "Can't create destination profile, luminance(%d) and/or gamma(%g) values are invalid", dstLuminance, dstGamma);
        FAIL();
    }

    // Calculate luminance scale and tonemapping
    COLORIST_ASSERT(srcLuminance > 0);
    COLORIST_ASSERT(dstLuminance > 0);
    luminanceScale = (float)srcLuminance / (float)dstLuminance;
    if (params->autoGrade) {
        // autoGrade ensures we're never scaling a pixel lower than the brighest
        // pixel in the source image, tonemapping is unnecessary.
        tonemap = clFalse;
    } else {
        // tonemap if we're scaling from a larger luminance range to a smaller range
        tonemap = (luminanceScale > 1.0f) ? clTrue : clFalse;
    }
    if (params->tonemap != CL_TONEMAP_AUTO) {
        tonemap = (params->tonemap == CL_TONEMAP_ON) ? clTrue : clFalse;
    }

    // Create the destination profile, or clone the source one
    if (dstProfile == NULL) {
        if (
            (params->primaries[0] > 0.0f) ||  // Custom primaries
            (srcGamma != dstGamma) ||         // Custom gamma
            (srcLuminance != dstLuminance) || // Custom luminance
            (params->description) ||          // Custom description
            (params->copyright)               // custom copyright
            )
        {
            clProfileCurve dstCurve;
            char * dstDescription = NULL;

            // Primaries
            if ((dstPrimaries.red[0] <= 0.0f) || (dstPrimaries.red[1] <= 0.0f) ||
                (dstPrimaries.green[0] <= 0.0f) || (dstPrimaries.green[1] <= 0.0f) ||
                (dstPrimaries.blue[0] <= 0.0f) || (dstPrimaries.blue[1] <= 0.0f) ||
                (dstPrimaries.white[0] <= 0.0f) || (dstPrimaries.white[1] <= 0.0f))
            {
                clContextLogError(C, "Can't create destination profile, destination primaries are invalid");
                FAIL();
            }

            // Gamma
            if (dstGamma == 0.0f) {
                // TODO: Support/pass-through any source curve
                clContextLogError(C, "Can't create destination profile, source profile's curve cannot be re-created as it isn't just a simple gamma curve");
                FAIL();
            }
            dstCurve.type = CL_PCT_GAMMA;
            dstCurve.gamma = dstGamma;

            // Luminance
            if (dstLuminance == 0) {
                dstLuminance = srcLuminance;
            }

            // Description
            if (params->description) {
                dstDescription = clContextStrdup(C, params->description);
            } else {
                dstDescription = clGenerateDescription(C, &dstPrimaries, &dstCurve, dstLuminance);
            }

            clContextLog(C, "profile", 0, "Creating new destination ICC profile: \"%s\"", dstDescription);
            dstProfile = clProfileCreate(C, &dstPrimaries, &dstCurve, dstLuminance, dstDescription);
            clFree(dstDescription);

            // Copyright
            if (params->copyright) {
                clContextLog(C, "profile", 1, "Setting copyright: \"%s\"", params->copyright);
                clProfileSetMLU(C, dstProfile, "cprt", "en", "US", params->copyright);
            }
        } else {
            // just clone the source one
            clContextLog(C, "profile", 0, "Using unmodified source ICC profile: \"%s\"", srcImage->profile->description);
            dstProfile = clProfileClone(C, srcImage->profile);
        }
    }

    if (resizing) {
        int resizedPixelsCount = dstWidth * dstHeight;
        float * resizedPixels = clAllocate(4 * sizeof(float) * resizedPixelsCount);
        clContextLog(C, "resize", 0, "Resizing %dx%d -> [filter:%s] -> %dx%d", srcImage->width, srcImage->height, clFilterToString(C, params->resizeFilter), dstWidth, dstHeight);
        timerStart(&t);
        clPixelMathResize(C, srcImage->width, srcImage->height, linearPixels, dstWidth, dstHeight, resizedPixels, params->resizeFilter);
        clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
        clFree(linearPixels);
        linearPixels = resizedPixels;
        linearPixelsCount = resizedPixelsCount;
    }

    // Create dstImage
    dstImage = clImageCreate(C, dstWidth, dstHeight, dstDepth, dstProfile);

    // Show image details
    clContextLog(C, "details", 0, "Source:");
    clImageDebugDump(C, srcImage, 0, 0, 0, 0, 1);
    clContextLog(C, "details", 0, "Destination:");
    clImageDebugDump(C, dstImage, 0, 0, 0, 0, 1);

    // Convert srcImage -> dstImage
    if (convertDirectly) {
        cmsHTRANSFORM directTransform;
        cmsUInt32Number srcFormat = (srcImage->depth == 16) ? TYPE_RGBA_16 : TYPE_RGBA_8;
        cmsUInt32Number dstFormat = (dstImage->depth == 16) ? TYPE_RGBA_16 : TYPE_RGBA_8;

        clContextLog(C, "convert", 0, "Converting directly...");
        timerStart(&t);
        directTransform = cmsCreateTransformTHR(C->lcms, srcImage->profile->handle, srcFormat, dstImage->profile->handle, dstFormat, INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE);
        doMultithreadedTransform(C, params->jobs, directTransform, srcImage->pixels, (srcImage->depth == 16) ? 8 : 4, dstImage->pixels, (dstImage->depth == 16) ? 8 : 4, dstImage->width * dstImage->height);
        cmsDeleteTransform(directTransform);
        clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
    } else {
        cmsHTRANSFORM fromLinear = cmsCreateTransformTHR(C->lcms, dstLinear->handle, TYPE_RGBA_FLT, dstImage->profile->handle, TYPE_RGBA_FLT, INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOOPTIMIZE);
        float * dstFloats; // final values in floating point, manually created to avoid cms eval'ing on a 16-bit basis for floats (yuck)
        COLORIST_ASSERT(linearPixels);

        if (srcLuminance != dstLuminance) {
            clContextLog(C, "luminance", 0, "Scaling luminance (%gx, %s)...", luminanceScale, tonemap ? "tonemap" : "clip");
            timerStart(&t);
            clPixelMathScaleLuminance(C, linearPixels, linearPixelsCount, luminanceScale, tonemap);
            clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
        }

        clContextLog(C, "convert", 0, "Performing color conversion...");
        timerStart(&t);
        dstFloats = clAllocate(4 * sizeof(float) * linearPixelsCount);
        doMultithreadedTransform(C, params->jobs, fromLinear, (uint8_t *)linearPixels, 4 * sizeof(float), (uint8_t *)dstFloats, 4 * sizeof(float), linearPixelsCount);
        cmsDeleteTransform(fromLinear);
        clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));

        if (haldImage) {
            int i;
            int haldDataCount = haldImage->width * haldImage->height;
            float * haldSrcFloats;
            float * haldData;

            clContextLog(C, "hald", 0, "Performing Hald CLUT postprocessing...");
            timerStart(&t);

            haldData = clAllocate(4 * sizeof(float) * haldDataCount);
            clPixelMathUNormToFloat(C, haldImage->pixels, haldImage->depth, haldData, haldDataCount);

            haldSrcFloats = dstFloats;
            dstFloats = clAllocate(4 * sizeof(float) * linearPixelsCount);

            for (i = 0; i < linearPixelsCount; ++i) {
                clPixelMathHaldCLUTLookup(C, haldData, haldDims, &haldSrcFloats[i * 4], &dstFloats[i * 4]);
            }

            clFree(haldData);
            clFree(haldSrcFloats);
            clContextLog(C, "timing", -1, TIMING_FORMAT, timerElapsedSeconds(&t));
        }
        clPixelMathFloatToUNorm(C, dstFloats, dstImage->pixels, dstImage->depth, linearPixelsCount);
        clFree(dstFloats);
    }

convertCleanup:
    if (dstProfile)
        clProfileDestroy(C, dstProfile);
    if (dstLinear)
        clProfileDestroy(C, dstLinear);
    if (linearPixels)
        clFree(linearPixels);
    if (haldImage)
        clImageDestroy(C, haldImage);

    if (!result) {
        if (dstImage) {
            clImageDestroy(C, dstImage);
            dstImage = NULL;
        }
    }
    return dstImage;
}

typedef struct clTransformTask
{
    cmsHTRANSFORM transform;
    void * inPixels;
    void * outPixels;
    int pixelCount;
} clTransformTask;

static void transformTaskFunc(clTransformTask * info)
{
    cmsDoTransform(info->transform, info->inPixels, info->outPixels, info->pixelCount);
}

void doMultithreadedTransform(clContext * C, int taskCount, cmsHTRANSFORM transform, uint8_t * srcPixels, int srcPixelBytes, uint8_t * dstPixels, int dstPixelBytes, int pixelCount)
{
    if (taskCount > pixelCount) {
        // This is a dumb corner case I'm not too worried about.
        taskCount = pixelCount;
    }

    if (taskCount == 1) {
        // Don't bother making any new threads
        cmsDoTransform(transform, srcPixels, dstPixels, pixelCount);
    } else {
        int pixelsPerTask = pixelCount / taskCount;
        int lastTaskPixelCount = pixelCount - (pixelsPerTask * (taskCount - 1));
        clTask ** tasks;
        clTransformTask * infos;
        int i;

        clContextLog(C, "convert", 1, "Using %d thread%s to pixel transform.", taskCount, (taskCount == 1) ? "" : "s");

        tasks = clAllocate(taskCount * sizeof(clTask *));
        infos = clAllocate(taskCount * sizeof(clTransformTask));
        for (i = 0; i < taskCount; ++i) {
            infos[i].transform = transform;
            infos[i].inPixels = &srcPixels[i * pixelsPerTask * srcPixelBytes];
            infos[i].outPixels = &dstPixels[i * pixelsPerTask * dstPixelBytes];
            infos[i].pixelCount = (i == (taskCount - 1)) ? lastTaskPixelCount : pixelsPerTask;
            tasks[i] = clTaskCreate(C, (clTaskFunc)transformTaskFunc, &infos[i]);
        }

        for (i = 0; i < taskCount; ++i) {
            clTaskDestroy(C, tasks[i]);
        }

        clFree(tasks);
        clFree(infos);
    }
}
