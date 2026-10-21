// MetalFX spatial upscaling of the whole frame in Aspyr's Metal presenter (its D3D Present is at 0x1000e8b78).
//
// Normally Aspyr sizes the drawable to the game's resolution (ASL_MetalView forceDrawableSizeWidth:height:) and
// Core Animation stretches it to the screen. With this on, the drawable stays at the screen's native size instead,
// Aspyr's 1:1 copy of the backbuffer into the drawable copies into our own texture, and right before the drawable is
// presented MetalFX upscales that into the drawable. If the game already renders at the screen's size nothing changes.
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/CAMetalLayer.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>

#include "metalfx.h"

// MetalFX needs macOS 13 and is weak-linked, metalfx_init checks it's there before anything here uses it
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

typedef void (*copy_texture_fn)(id, SEL, id, NSUInteger, NSUInteger, MTLOrigin, MTLSize, id, NSUInteger, NSUInteger, MTLOrigin);

static int metalfx_ready = 0; // MetalFX is there and the drawable size hook is installed
static int metalfx_failed = 0; // the scaler couldn't be made, so stop keeping the drawable at native size

// ASL_MetalView
static IMP orig_updateDrawableSize = NULL;
static ptrdiff_t view_size_offset, forced_width_offset, forced_height_offset;
static SEL sel_contentScaleFactor, sel_metalLayer;
static id last_view = nil; // not retained, only used to put the drawable size back if MetalFX fails

// only touched on Aspyr's render thread, in its present function
static id<MTLFXSpatialScaler> scaler = nil;
static id<MTLTexture> input_texture = nil, output_texture = nil;
static NSUInteger input_width, input_height, output_width, output_height;
static MTLPixelFormat input_format, output_format;
static id<MTLTexture> pending_input = nil, pending_drawable_texture = nil; // set by the copy, used by the present

// Aspyr's updateDrawableSize sets the drawable to the forced size (the game's resolution) when there is one, and to
// the view's size times its scale factor otherwise. Keep the forced size for everything else that reads it, but give
// the layer the view's native pixels when they're bigger, so MetalFX can upscale into them.
static void updateDrawableSize_hook(id self, SEL sel) {
    ((void (*)(id, SEL))orig_updateDrawableSize)(self, sel);
    if (metalfx_failed)
        return;
    int forced_width = *(int *)((char *)self + forced_width_offset);
    int forced_height = *(int *)((char *)self + forced_height_offset);
    if (forced_width <= 0 || forced_height <= 0)
        return;
    // the view's size in pixels. Aspyr's _viewSize and contentScaleFactor are in points here (1728x1117 with a
    // scale of 1 on a 3456x2234 MacBook Pro), so go by the view's bounds and its window's backing scale instead.
    int native_width, native_height;
    if ([self isKindOfClass:[NSView class]] && ((NSView *)self).window != nil) {
        NSRect backing = [(NSView *)self convertRectToBacking:((NSView *)self).bounds];
        native_width = (int)lround(backing.size.width);
        native_height = (int)lround(backing.size.height);
    } else {
        CGSize view_size = *(CGSize *)((char *)self + view_size_offset);
        double scale = ((double (*)(id, SEL))objc_msgSend)(self, sel_contentScaleFactor);
        native_width = (int)lround(view_size.width * scale);
        native_height = (int)lround(view_size.height * scale);
    }

    static int last_forced_width = -1, last_forced_height = -1, last_native_width = -1, last_native_height = -1;
    int changed = forced_width != last_forced_width || forced_height != last_forced_height ||
                  native_width != last_native_width || native_height != last_native_height;
    last_forced_width = forced_width; last_forced_height = forced_height;
    last_native_width = native_width; last_native_height = native_height;

    if (native_width < forced_width || native_height < forced_height ||
        (native_width == forced_width && native_height == forced_height)) {
        if (changed)
            printf("MetalFX: the game's resolution %ix%i already fills the %ix%i screen, nothing to upscale\n",
                   forced_width, forced_height, native_width, native_height);
        return;
    }
    last_view = self;
    CAMetalLayer *layer = ((id (*)(id, SEL))objc_msgSend)(self, sel_metalLayer);
    layer.drawableSize = CGSizeMake(native_width, native_height);
}

// (re)makes the scaler and its textures for these sizes and formats, returns 0 if MetalFX can't do it
static int ensure_resources(id<MTLDevice> device, NSUInteger in_width, NSUInteger in_height, MTLPixelFormat in_format,
                            NSUInteger out_width, NSUInteger out_height, MTLPixelFormat out_format) {
    if (scaler != nil && in_width == input_width && in_height == input_height && in_format == input_format &&
        out_width == output_width && out_height == output_height && out_format == output_format)
        return 1;
    [scaler release];
    [input_texture release];
    [output_texture release];
    scaler = nil;
    input_texture = output_texture = nil;

    MTLFXSpatialScalerDescriptor *desc = [[NSClassFromString(@"MTLFXSpatialScalerDescriptor") alloc] init];
    desc.inputWidth = in_width;
    desc.inputHeight = in_height;
    desc.outputWidth = out_width;
    desc.outputHeight = out_height;
    desc.colorTextureFormat = in_format;
    desc.outputTextureFormat = out_format;
    // the game's BGRA8 backbuffer holds gamma-encoded colour, an _sRGB format would hand MetalFX linear values
    BOOL srgb = in_format == MTLPixelFormatBGRA8Unorm_sRGB || in_format == MTLPixelFormatRGBA8Unorm_sRGB;
    desc.colorProcessingMode = srgb ? MTLFXSpatialScalerColorProcessingModeLinear : MTLFXSpatialScalerColorProcessingModePerceptual;
    scaler = [desc newSpatialScalerWithDevice:device];
    [desc release];
    if (scaler == nil)
        return 0;

    MTLTextureDescriptor *texture_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:in_format
                                                                                            width:in_width
                                                                                           height:in_height
                                                                                        mipmapped:NO];
    texture_desc.storageMode = MTLStorageModePrivate;
    texture_desc.usage = scaler.colorTextureUsage;
    input_texture = [device newTextureWithDescriptor:texture_desc];
    texture_desc.pixelFormat = out_format;
    texture_desc.width = out_width;
    texture_desc.height = out_height;
    texture_desc.usage = scaler.outputTextureUsage;
    output_texture = [device newTextureWithDescriptor:texture_desc];
    if (input_texture == nil || output_texture == nil) {
        [scaler release];
        scaler = nil;
        return 0;
    }
    input_width = in_width;
    input_height = in_height;
    input_format = in_format;
    output_width = out_width;
    output_height = out_height;
    output_format = out_format;
    printf("MetalFX upscaling %lux%lu to %lux%lu\n", (unsigned long)in_width, (unsigned long)in_height,
           (unsigned long)out_width, (unsigned long)out_height);
    return 1;
}

// called instead of objc_msgSend for Aspyr's [blitEncoder copyFromTexture:backbuffer ... toTexture:drawable.texture ...]
static void copy_to_drawable(id<MTLBlitCommandEncoder> encoder, SEL sel, id<MTLTexture> source, NSUInteger source_slice,
                             NSUInteger source_level, MTLOrigin source_origin, MTLSize size, id<MTLTexture> drawable_texture,
                             NSUInteger destination_slice, NSUInteger destination_level, MTLOrigin destination_origin) {
    pending_input = pending_drawable_texture = nil;
    NSUInteger drawable_width = drawable_texture.width, drawable_height = drawable_texture.height;
    int upscale = metalfx_ready && !metalfx_failed && size.width <= drawable_width && size.height <= drawable_height &&
                  (size.width < drawable_width || size.height < drawable_height) && source_slice == 0 && source_level == 0 &&
                  source_origin.x == 0 && source_origin.y == 0 && destination_origin.x == 0 && destination_origin.y == 0;
    if (!upscale) {
        ((copy_texture_fn)objc_msgSend)(encoder, sel, source, source_slice, source_level, source_origin, size, drawable_texture,
                                        destination_slice, destination_level, destination_origin);
        return;
    }
    if (!ensure_resources(encoder.device, size.width, size.height, source.pixelFormat, drawable_width, drawable_height,
                          drawable_texture.pixelFormat)) {
        printf("MetalFX couldn't upscale %lux%lu to %lux%lu, not upscaling\n", (unsigned long)size.width,
               (unsigned long)size.height, (unsigned long)drawable_width, (unsigned long)drawable_height);
        metalfx_failed = 1;
        // this frame goes out un-scaled in the corner, put the drawable back to the game's size for the next ones
        if (last_view != nil)
            ((void (*)(id, SEL))orig_updateDrawableSize)(last_view, sel_registerName("updateDrawableSize"));
        ((copy_texture_fn)objc_msgSend)(encoder, sel, source, source_slice, source_level, source_origin, size, drawable_texture,
                                        destination_slice, destination_level, destination_origin);
        return;
    }
    // MetalFX can read the backbuffer itself if it was made readable by shaders and is exactly the frame's size
    MTLTextureUsage needed = scaler.colorTextureUsage;
    if ((source.usage & needed) == needed && source.width == size.width && source.height == size.height &&
        source.textureType == MTLTextureType2D) {
        pending_input = source;
    } else {
        [encoder copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:source_origin sourceSize:size
                       toTexture:input_texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        pending_input = input_texture;
    }
    pending_drawable_texture = drawable_texture;
}

void *metalfx_copy_function(void) {
    return (void *)copy_to_drawable;
}

// Aspyr has ended its blit encoder by now, so MetalFX and our copy into the drawable can go in the command buffer
void metalfx_before_present(void *command_buffer) {
    id<MTLCommandBuffer> cb = (id<MTLCommandBuffer>)command_buffer;
    id<MTLTexture> input = pending_input, drawable_texture = pending_drawable_texture;
    pending_input = pending_drawable_texture = nil;
    if (input == nil || drawable_texture == nil || scaler == nil)
        return;
    scaler.colorTexture = input;
    scaler.outputTexture = output_texture;
    scaler.inputContentWidth = input_width;
    scaler.inputContentHeight = input_height;
    [scaler encodeToCommandBuffer:cb];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:output_texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(output_width, output_height, 1) toTexture:drawable_texture destinationSlice:0
         destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
}

static ptrdiff_t ivar_offset(Class cls, const char *name) {
    Ivar ivar = class_getInstanceVariable(cls, name);
    return ivar != NULL ? ivar_getOffset(ivar) : -1;
}

int metalfx_init(void) {
    @autoreleasepool {
        if (NSClassFromString(@"MTLFXSpatialScalerDescriptor") == nil) {
            printf("MetalFX isn't available (it needs macOS 13 or later), not upscaling\n");
            return 0;
        }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        BOOL supported = device != nil && [NSClassFromString(@"MTLFXSpatialScalerDescriptor") supportsDevice:device];
        [device release];
        if (!supported) {
            printf("MetalFX doesn't support this GPU, not upscaling\n");
            return 0;
        }

        Class view = objc_getClass("ASL_MetalView");
        sel_contentScaleFactor = sel_registerName("contentScaleFactor");
        sel_metalLayer = sel_registerName("metalLayer");
        Method update = view != Nil ? class_getInstanceMethod(view, sel_registerName("updateDrawableSize")) : NULL;
        view_size_offset = view != Nil ? ivar_offset(view, "_viewSize") : -1;
        forced_width_offset = view != Nil ? ivar_offset(view, "_forcedDrawableSizeWidth") : -1;
        forced_height_offset = view != Nil ? ivar_offset(view, "_forcedDrawableSizeHeight") : -1;
        if (update == NULL || view_size_offset < 0 || forced_width_offset < 0 || forced_height_offset < 0 ||
            class_getInstanceMethod(view, sel_contentScaleFactor) == NULL || class_getInstanceMethod(view, sel_metalLayer) == NULL) {
            printf("MetalFX: couldn't find Aspyr's Metal view, not upscaling\n");
            return 0;
        }
        orig_updateDrawableSize = method_setImplementation(update, (IMP)updateDrawableSize_hook);
        metalfx_ready = 1;
        printf("MetalFX upscaling is on, it upscales when the game's resolution is lower than the screen's\n");
        return 1;
    }
}
