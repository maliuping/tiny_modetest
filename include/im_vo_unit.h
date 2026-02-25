#ifndef __IM_VO_UNIT_H__
#define __IM_VO_UNIT_H__
#include <time.h>
#include <stdbool.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "im_hal_fb.h"
#include "im_vo_helper.h"


#define LOGI(fmt, ...)  printf("[I][%s][%s:%d] " fmt "\n", __TAG__,__FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOGE(fmt, ...)  fprintf(stderr, "[E][%s][%s:%d] " fmt "\n", __TAG__,__FUNCTION__, __LINE__, ##__VA_ARGS__)

typedef enum vo_pix_fmt {
	PIX_FMT_ARGB8888,
	PIX_FMT_ABGR8888,
	PIX_FMT_RGBA8888,
	PIX_FMT_BGRA8888,
	PIX_FMT_XRGB8888,
	PIX_FMT_RGB888,
	PIX_FMT_BGR888,
	PIX_FMT_YUV420_8_SP,
	PIX_FMT_YUV422_8_PACK, // yuyv, 1 plane
	PIX_FMT_RGBA1010102,
	PIX_FMT_ARGB2101010,
	PIX_FMT_INVALID,
} vo_pix_fmt_t;



typedef enum drm_buf_type {
	DRM_BUF_DMABUF,
	DRM_BUF_DUMB,
} drm_buf_type_t;


typedef struct drm_buffer {
	drm_buf_type_t type;

	uint32_t width;
	uint32_t height;
	uint32_t format;  // DRM_FORMAT_XXXX
	uint32_t nplanes; // number of planes

	uint32_t handles[4];
	uint32_t pitches[4];
	uint32_t offsets[4];

	uint32_t fb_id;

	/* dma-buf */
	bool gem_imported;

	/* dumb only */
	uint32_t dumb_handle;
	uint32_t dumb_size;
	void *map;
} drm_buffer_t;

// event sync
typedef struct vo_drm_event_ctx {
	int fd; // drm fd

	drmEventContext evctx; // register drm event callback

	pthread_t thread; // drm event poll thread

	int wake_fd; // eventfd, wake poll
	bool running; // control drm event thread

	bool atomic_committed;
	pthread_mutex_t lock;
	pthread_cond_t  commit_cond;

	/* FPS stats */

	uint64_t flip_count;
	struct timespec flip_start_ts;

	uint64_t vblank_count;
	struct timespec vblank_start_ts;
	bool vblank_ordered;

	vo_ringbuf_t ring;

} vo_drm_event_ctx_t;

typedef struct vo_unit {
	int drm_fd;  // drm fd

	int mode;   //
	int test_vblank;
	int width;  // input image width, im_hal_fb_alloc used
	int height; // im_hal_fb_alloc used
	int vrefresh;
	char file_name[256];   // input image file
	int hsize_align;  // input image hsize alignment, im_hal_fb_alloc used
	int format; // input image format, im_hal_fb_alloc used

    uint32_t conn_id;
    uint32_t crtc_id;
    uint32_t plane_id;
    drmModeModeInfo drm_mode;

	im_fb_t *fb;

	bool notified;

	bool exiting; // main process exit
	pthread_t dump_thread;

	vo_drm_event_ctx_t drm_event;
} vo_unit_t;



int drm_display_from_dmabuf_atomic(const char *card, im_fb_t *fb);

int drm_display_from_dmabuf(int fd, im_fb_t *fb, drm_buffer_t *buf);

int find_connector_crtc_plane(vo_unit_t *vo);

int drm_buf_create_dumb(int fd,
		uint32_t w,
		uint32_t h,
		uint32_t format,
		drm_buffer_t *buf);

void drm_buf_fill_color(drm_buffer_t *b, uint32_t color);
int drm_buf_add_fb(int fd, drm_buffer_t *buf);
int drm_atomic_modeset_with_plane(vo_unit_t *vo, int fd,
	uint32_t conn_id,
	uint32_t crtc_id,
	uint32_t plane_id,
	drmModeModeInfo *mode,
	drm_buffer_t *buf);

int drm_atomic_page_flip(vo_unit_t *vo, int fd, uint32_t plane_id, drm_buffer_t *buf);

void drm_buf_destroy(int fd, drm_buffer_t *buf);

int vo_drm_event_ctx_init(vo_drm_event_ctx_t *ctx, int drm_fd);
void vo_drm_event_ctx_deinit(vo_drm_event_ctx_t *ctx);

int vo_drm_vblank(vo_drm_event_ctx_t *ctx);
#endif /* im_vo_unit*/