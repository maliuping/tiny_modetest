/**
 * (C) Copyright 2024, Imvision Co., Ltd
 * This file is classified as confidential level C3 within Imvision
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author         Notes
 * 2025-05-16     liubinggang     Initialize.
 */

#ifndef __IM_HAL_G2D_H__
#define __IM_HAL_G2D_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <im_hal_fb.h>
#include "vg_lite.h"

typedef struct im_g2d_info      *im_g2d_handle_t;
typedef vg_lite_float_t          im_g2d_float_t;
/**
 * @brief 32-bit RGBA color value.
 *
 * The color channels are stored in little-endian order:
 * | R (7:0) | G (15:8) | B (23:16) | A (31:24) |
 */
typedef uint32_t                 im_g2d_color_t;

typedef enum {
    IM_ROTATION_0     = 0,
    IM_ROTATION_90    = 1,
    IM_ROTATION_180   = 2,
    IM_ROTATION_270   = 3,
    IM_ROTATION_BUTT
} im_rotation_e;

typedef enum {
    IM_ASPECT_RATIO_STRETCH = 0,
    IM_ASPECT_RATIO_AUTO    = 1,
    IM_ASPECT_RATIO_MANUAL  = 2,
    IM_ASPECT_RATIO_BUTT
} im_aspect_ratio_e;

typedef enum {
    IM_ASPECT_RATIO_HORIZONTAL_LEFT   = 1,
    IM_ASPECT_RATIO_HORIZONTAL_CENTER = 2,
    IM_ASPECT_RATIO_HORIZONTAL_RIGHT  = 3,

    IM_ASPECT_RATIO_VERTICAL_TOP      = IM_ASPECT_RATIO_HORIZONTAL_LEFT,
    IM_ASPECT_RATIO_VERTICAL_CENTER   = IM_ASPECT_RATIO_HORIZONTAL_CENTER,
    IM_ASPECT_RATIO_VERTICAL_BOTTOM   = IM_ASPECT_RATIO_HORIZONTAL_RIGHT
} im_aspect_ratio_align_e;

typedef struct {
    int16_t  x;
    int16_t  y;
    uint16_t w;
    uint16_t h;
} im_rect_t;

typedef struct {
    int16_t x;
    int16_t y;
} im_point_t;

typedef enum {
    IM_FILTER_POINT,
    IM_FILTER_LINEAR,
    IM_FILTER_BI_LINEAR,
    IM_FILTER_GAUSSIAN
} im_scale_filter_t;

typedef struct {
    im_aspect_ratio_e           mode;         /* Reserved */
    uint32_t                    bg_color;     /* Reserved */
    union {
        im_aspect_ratio_align_e aligns[2]; /* Reserved */
        im_rect_t               rect;      /* Reserved */
    };
    im_scale_filter_t           scale_filter;
} im_aspect_ratio_t;

typedef struct {
    bool     en;
    uint16_t inv;       /* 0: within threshold, 1: outside threshold */
    uint32_t key_low;   /* Min value of color key: 0xRRGGBB */
    uint32_t key_high;  /* Max value of color key: 0xRRGGBB */
} im_colorkey_t;

typedef struct {
    im_fb_t           *fb;
    im_point_t        offset;
    im_colorkey_t     colorkey;
    vg_lite_blend_t   blend_mode;
    bool              en_g_alpha;
    uint8_t           alpha;
} im_overlay_t;

typedef struct {
    bool     en;
    int16_t  crop_x;
    int16_t  crop_y;
    int16_t  crop_w;
    int16_t  crop_h;
} im_crop_info_t;

typedef struct {
    uint16_t thick;
    uint16_t alpha;
    uint32_t color;
    bool     solid; /* true = fill rect with color */
} im_gdi_attr_t;

typedef struct im_g2d_init_attr {
    /* Reserved for future extensions */
} im_g2d_init_attr_t;

/**
 * @brief Initialize the G2D (2D graphics engine) hardware.
 *
 * This function initializes the G2D hardware and prepares internal
 * driver or hardware state for subsequent G2D operations.
 *
 * @param[in]  attr       Pointer to initialization attributes (can be NULL for default).
 * @return im_g2d_handle_t
 * @retval non-NULL       Valid G2D handle for subsequent operations.
 * @retval NULL           Initialization failed.
 *
 * @note The returned handle must be passed to all subsequent G2D operations.
 *       Caller must call im_hal_g2d_close() to release resources when done.
 */
im_g2d_handle_t im_hal_g2d_init(im_g2d_init_attr_t *attr);

/**
 * @brief Deinitialize the G2D (2D graphics engine) hardware.
 *
 * @param[in]  handle     G2D handle obtained from im_hal_g2d_init().
 * @return im_res_t
 * @retval IM_SUCCESS     Resources released successfully.
 * @retval <0             Operation failed. Returns a negative error code.
 *
 * @note After this call, the handle becomes invalid and must not be used.
 *       Caller must ensure all pending operations are completed before closing.
 */
im_res_t im_hal_g2d_close(im_g2d_handle_t handle);

/**
 * @brief Perform image rotation.
 *
 * @param[in]  handle     G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src        Pointer to the source image framebuffer.
 * @param[out] dst        Pointer to the destination image framebuffer.
 * @param[in]  rotation   Rotation angle (defined in im_rotation_e, e.g., 90°, 180°, 270°).
 * @return im_res_t
 * @retval IM_SUCCESS     Operation completed successfully.
 * @retval <0             Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_rotation(im_g2d_handle_t handle,
                             im_fb_t *src, im_fb_t *dst, im_rotation_e rotation);

/**
 * @brief Resize an image, optionally maintaining aspect ratio.
 *
 * @param[in]  handle        G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src           Pointer to the source image framebuffer.
 * @param[out] dst           Pointer to the destination image framebuffer.
 * @param[in]  aspectration  Pointer to the aspect ratio control structure.
 * @return im_res_t
 * @retval IM_SUCCESS        Operation completed successfully.
 * @retval <0                Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_resize(im_g2d_handle_t handle,
                           im_fb_t *src, im_fb_t *dst, im_aspect_ratio_t *aspectration);

/**
 * @brief Perform alpha blending (overlay with transparency).
 *
 * @param[in]  handle     G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src        Pointer to the source image framebuffer (background image).
 * @param[in]  overlay    Pointer to the overlay structure.
 * @param[out] dst        Pointer to the destination image framebuffer.
 * @return im_res_t
 * @retval IM_SUCCESS     Operation completed successfully.
 * @retval <0             Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_alpha_blending(im_g2d_handle_t handle,
                                   im_fb_t *src, im_overlay_t *overlay, im_fb_t *dst);

/**
 * @brief Crop a rectangular region from the source image.
 *
 * @param[in]  handle     G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src        Pointer to the source image framebuffer.
 * @param[out] dst        Pointer to the destination image framebuffer.
 * @param[in]  cropinfo   Pointer to the crop parameters.
 * @return im_res_t
 * @retval IM_SUCCESS     Operation completed successfully.
 * @retval <0             Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_crop(im_g2d_handle_t handle,
                         im_fb_t *src, im_fb_t *dst, im_crop_info_t *cropinfo);

/**
 * @brief Copy a rectangular region of pixels from source to destination image.
 *
 * @param[in]  handle     G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src        Pointer to the source image framebuffer.
 * @param[out] dst        Pointer to the destination image framebuffer.
 * @param[in]  src_rect   Rectangle region (x, y, w, h) to copy from the source.
 * @param[in]  dst_pos    Top-left position (x, y) in the destination where the rectangle is placed.
 * @return im_res_t
 * @retval IM_SUCCESS     Operation completed successfully.
 * @retval <0             Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_copy_frame(im_g2d_handle_t handle, im_fb_t *src, im_fb_t *dst,
                               im_rect_t *src_rect, im_point_t dst_pos);

/**
 * @brief Draw multiple points on an image.
 *
 * @param[in]     handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in,out] src      Pointer to the framebuffer to draw on (modified in place).
 * @param[in]     attr     Drawing attributes (e.g., color, thickness).
 * @param[in]     point[]  Array of points to draw.
 * @param[in]     num      Number of points in the array.
 * @return im_res_t
 * @retval IM_SUCCESS      Operation completed successfully.
 * @retval <0              Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_draw_points(im_g2d_handle_t handle, im_fb_t *src, im_gdi_attr_t attr,
                                im_point_t point[], im_size_t num);

/**
 * @brief Draw multiple rectangles on an image.
 *
 * @param[in]     handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in,out] src      Pointer to the framebuffer to draw on (modified in place).
 * @param[in]     attr     Drawing attributes (e.g., border color, line width).
 * @param[in]     box[]    Array of rectangles to draw.
 * @param[in]     num      Number of rectangles in the array.
 * @return im_res_t
 * @retval IM_SUCCESS      Operation completed successfully.
 * @retval <0              Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_draw_rects(im_g2d_handle_t handle, im_fb_t *src, im_gdi_attr_t attr,
                               im_rect_t box[], im_size_t num);

/**
 * @brief Clear the content of a framebuffer.
 *
 * @param[in]  handle    G2D handle obtained from im_hal_g2d_init().
 * @param[in,out] frame  Pointer to the framebuffer to clear.
 * @param[in]  color     Color value used to fill the framebuffer.
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_frame_clear(im_g2d_handle_t handle, im_fb_t *frame, im_g2d_color_t color);

/**
 * @brief Reset the current operation.
 *
 * @param[in] handle   G2D handle obtained from im_hal_g2d_init().
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_ops_reset(im_g2d_handle_t handle);

/**
 * @brief Configure crop information for subsequent operations.
 *
 * Defines a rectangular region of the source frame buffer to be used in
 * blit or blend operations. Only pixels within the crop region will be
 * processed and transferred to the destination.
 *
 * @param[in] handle     G2D handle obtained from im_hal_g2d_init().
 * @param[in] cropinfo   Pointer to cropping region configuration.
 * @return im_res_t
 * @retval IM_SUCCESS    Operation completed successfully.
 * @retval <0            Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_ops_cropinfo(im_g2d_handle_t handle, im_crop_info_t *cropinfo);

/**
 * @brief Apply a rotation to the current operation.
 *
 * @param[in]  handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in]  rotation   Rotation angle (defined in im_rotation_e, e.g., 90°, 180°, 270°).
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_ops_rotate(im_g2d_handle_t handle, im_rotation_e rotation);

/**
 * @brief Apply scaling to the current operation.
 *
 * @param[in]  handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in]  scale_x  Scaling factor along the X axis.
 * @param[in]  scale_y  Scaling factor along the Y axis.
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_ops_scale(im_g2d_handle_t handle,
                              im_g2d_float_t scale_x, im_g2d_float_t scale_y);

/**
 * @brief Apply translation to the current operation.
 *
 * @param[in]  handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in]  trans_x  Translation offset along the X axis.
 * @param[in]  trans_y  Translation offset along the Y axis.
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_ops_translate(im_g2d_handle_t handle,
                                  im_g2d_float_t trans_x,
                                  im_g2d_float_t trans_y);

/**
 * @brief Perform a basic g2d operation from a source frame
 *        buffer to a destination frame buffer.
 *
 * @param[in]  handle   G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src      Source frame buffer.
 * @param[out] dst      Destination frame buffer.
 * @return im_res_t
 * @retval IM_SUCCESS   Operation completed successfully.
 * @retval <0           Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_submit(im_g2d_handle_t handle, im_fb_t *src, im_fb_t *dst);

/**
 * @brief Perform a g2d operation with alpha blending applied.
 *
 * @param[in]  handle    G2D handle obtained from im_hal_g2d_init().
 * @param[in]  src       Source frame buffer.
 * @param[in]  overlay   Overlay blending configuration (alpha, position, etc.).
 * @param[out] dst       Destination frame buffer.
 * @return im_res_t
 * @retval IM_SUCCESS    Operation completed successfully.
 * @retval <0            Operation failed. Returns a negative error code.
 */
im_res_t im_hal_g2d_submit_blend(im_g2d_handle_t handle, im_fb_t *src,
                                 im_overlay_t *overlay, im_fb_t *dst);

#ifdef __cplusplus
}
#endif

#endif /* __IM_HAL_G2D_H__ */
