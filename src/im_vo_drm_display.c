#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <limits.h>

#include "im_hal_fb.h"  //  im_fb_t, im_hal_mem_get_fd()
#include "im_vo_unit.h"

#define __TAG__ "drm_display"


static uint32_t im_pixfmt_to_drm(im_pix_fmt_t fmt)
{
	switch (fmt) {
		case IM_PIX_FMT_ARGB8888: return DRM_FORMAT_ARGB8888;
		case IM_PIX_FMT_ABGR8888: return DRM_FORMAT_ABGR8888;
		case IM_PIX_FMT_RGBA8888: return DRM_FORMAT_RGBA8888;
		case IM_PIX_FMT_BGRA8888: return DRM_FORMAT_BGRA8888;
		case IM_PIX_FMT_XRGB8888: return DRM_FORMAT_XRGB8888;
		case IM_PIX_FMT_RGB888:   return DRM_FORMAT_RGB888;
		case IM_PIX_FMT_BGR888:   return DRM_FORMAT_BGR888;
		case IM_PIX_FMT_YUV420_8_SP: return DRM_FORMAT_NV12;
		case IM_PIX_FMT_YUV422_8_PACK: return DRM_FORMAT_YUYV;
		case IM_PIX_FMT_RGBA1010102: return DRM_FORMAT_RGBA1010102;
		case IM_PIX_FMT_ARGB2101010: return DRM_FORMAT_ARGB2101010;
		default: return DRM_FORMAT_INVALID;
	}
}


static int mode_vrefresh(const drmModeModeInfo *m)
{
	// not distinguish interlace, dblscan
	if (m->vrefresh)
		return m->vrefresh;

	if (m->htotal && m->vtotal)
		return (m->clock * 1000) / (m->htotal * m->vtotal);

	return 0;
}




static int find_best_mode(drmModeConnector *conn,
                          int width, int height, int vrefresh,
                          drmModeModeInfo *out)
{
	drmModeModeInfo *best = NULL;
	int best_diff = 1;

	for (int i = 0; i < conn->count_modes; i++) {
		drmModeModeInfo *m = &conn->modes[i];
		int vr = mode_vrefresh(m);

		LOGI("Mode %d: %s %dx%d@%dHz flags=0x%x type=0x%x",
				i, m->name, m->hdisplay, m->vdisplay, vr,
				m->flags, m->type);

		if (width == -1 && height == -1 && vrefresh == -1) {
			*out = *m;
			return 0;
		}

		if (m->hdisplay != width || m->vdisplay != height)
			continue;

		if (vrefresh > 0) {
			int diff = abs(vr - vrefresh);

			/* perfect match */
			if (diff == 0) {
				*out = *m;
				return 0;
			}

			if (diff < best_diff) {
				best = m;
				best_diff = diff;
			}
		} else {
			if (m->type & DRM_MODE_TYPE_PREFERRED) {
				*out = *m;
				return 0;
			}

			if (!best)
				best = m; // fallback
		}
	}

	if (best) {
		*out = *best;
		return 0;
	}

	return -1;
}


/*
 * find connected connector, crtc and plane
 *
 * fd: drm device fd
 * conn_id: output connector id
 * crtc_id: output crtc id
 * plane_id: output plane id
 * mode: output mode
 *
 * return 0 on success, -1 on failure
 */
// int find_connector_crtc_plane(int fd,
// 									uint32_t *conn_id,
// 									uint32_t *crtc_id,
// 									uint32_t *plane_id,
// 									drmModeModeInfo *mode)
int find_connector_crtc_plane(vo_unit_t *vo)
{

	drmModeRes *res = NULL;
	drmModeConnector *conn = NULL;

	int fd = vo->drm_fd;
	drmModeModeInfo *mode = &(vo->drm_mode);

	res = drmModeGetResources(fd);
	if (!res) {
		LOGE("drmModeGetResources failed: %s", strerror(errno));
		return -errno;
	}

	for (int i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection != DRM_MODE_CONNECTED ||
			conn->count_modes == 0) {
			drmModeFreeConnector(conn);
			conn = NULL;
			continue;
		}

		if (find_best_mode(conn,
						   vo->width,
						   vo->height,
						   vo->vrefresh,
						   mode) == 0) {
			vo->conn_id = conn->connector_id;
			break;
		}
	}

	if (!conn) {
		LOGE("no connected connector found");
		drmModeFreeResources(res);
		return -1;
	}

	drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
	vo->crtc_id = enc ? enc->crtc_id : res->crtcs[0];
	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);

	drmModePlaneRes *pres = drmModeGetPlaneResources(fd);
	if (!pres) {
		drmModeFreeResources(res);
		LOGE("drmModeGetPlaneResources failed");
		return -1;
	}
	vo->plane_id = pres->planes[0];
	drmModeFreePlaneResources(pres);
	drmModeFreeResources(res);

	return 0;
}

/*
 * get property id by name
 *
 * fd: drm device fd
 * obj_id: object id
 * obj_type: object type, e.g. DRM_MODE_OBJECT_CRTC
 * name: property name
 *
 * return property id, 0 on failure
 */
static uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *name)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj_id, obj_type);
	if (!props) return 0;
	uint32_t prop_id = 0;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
		if (p) {
			if (strcmp(p->name, name) == 0)
				prop_id = p->prop_id;
			drmModeFreeProperty(p);
			if (prop_id)
				break;
		}
	}
	drmModeFreeObjectProperties(props);
	return prop_id;
}

/*
 * create drm framebuffer from dma-buf fd，and fill drm_buffer_t
 * fd: drm device fd
 * fb: im_fb_t framebuffer
 * buf: output drm_buffer_t
 *
 * return 0 on success, -1 on failure
 */

int drm_display_from_dmabuf(int fd, im_fb_t *fb, drm_buffer_t *buf)
{
	uint32_t gem_handle;

	int dma_fd = im_hal_mem_get_fd(fb->mem_handle);
	if (dma_fd < 0) {
		LOGE("im_hal_mem_get_fd failed");
		return -1;
	}

	/* it takes a DRM device file descriptor, a DMA-BUF file descriptor,
		* and returns a GEM handle that can be used for GPU operations
		*/
	if (drmPrimeFDToHandle(fd, dma_fd, &gem_handle) != 0) {
		LOGE("drmPrimeFDToHandle failed: %s", strerror(errno));
		return -1;
	}

	memset(buf, 0, sizeof(drm_buffer_t));

	buf->type = DRM_BUF_DMABUF;
	buf->gem_imported = true;
	buf->width = fb->info.width;
	buf->height = fb->info.height;
	buf->format = im_pixfmt_to_drm(fb->info.format.pix_fmt);
	buf->nplanes = fb->nplanes;

	for(int i = 0; i < fb->nplanes && i < 4; i++) {
		buf->handles[i] = gem_handle;
		buf->pitches[i] = fb->planes[i].hstride;
		buf->offsets[i] = fb->planes[i].offset;
		LOGI("plane %d: handle=%u, pitch=%u, offset=%u",
				i, buf->handles[i], buf->pitches[i], buf->offsets[i]);
	}

	return 0;
}


/*
 * create dumb buffer and fill drm_buffer_t
 * fd: drm device fd
 * w: width
 * h: height
 * format: DRM_FORMAT_XXXX
 * buf: output drm_buffer_t
 *
 * return 0 on success, -1 on failure
 */

int drm_buf_create_dumb(int fd,
						uint32_t w,
						uint32_t h,
						uint32_t format,
						drm_buffer_t *buf)
{
	struct drm_mode_create_dumb creq = {0};
	struct drm_mode_map_dumb mreq = {0};

	memset(buf, 0, sizeof(*buf));

	creq.width  = w;
	creq.height = h;
	creq.bpp    = (format == DRM_FORMAT_NV12) ? 8 : 32;

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
		return -1;

	buf->type        = DRM_BUF_DUMB;
	buf->width       = w;
	buf->height      = h;
	buf->format      = format;
	buf->dumb_handle = creq.handle;
	buf->dumb_size   = creq.size;

	if (format == DRM_FORMAT_NV12) {
		buf->nplanes = 2;

		buf->handles[0] = creq.handle;
		buf->handles[1] = creq.handle;

		buf->pitches[0] = creq.pitch;
		buf->pitches[1] = creq.pitch;

		buf->offsets[0] = 0;
		buf->offsets[1] = w * h;
	} else {
		buf->nplanes = 1;
		buf->handles[0] = creq.handle;
		buf->pitches[0] = creq.pitch;
		buf->offsets[0] = 0;
		LOGI("dumb buffer created: w=%u, h=%u, pitch=%u, size=%u", buf->width, buf->height, buf->pitches[0], buf->dumb_size);
	}

	mreq.handle = creq.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);

	buf->map = mmap(NULL, creq.size,
					PROT_READ | PROT_WRITE,
					MAP_SHARED, fd, mreq.offset);
	if (buf->map == MAP_FAILED)
		return -1;

	LOGI("dumb buffer mapped at %p", buf->map);

	return 0;
}

/*
 * fill dumb buffer from file
 */

void drm_buf_fill_file(drm_buffer_t *buf, const char *file)
{
	FILE *fp = fopen(file, "rb");

	fread(buf->map, 1, buf->dumb_size, fp);
	fclose(fp);
}


/*
 * fill dumb buffer with color
 */
void drm_buf_fill_color(drm_buffer_t *b, uint32_t color)
{
	if (b->format == DRM_FORMAT_NV12) {
		// memset(b->map, y, b->width * b->height);
		// memset(b->map + b->offsets[1], 128, b->width * b->height / 2);
	} else {
		uint32_t *pix = (uint32_t *)b->map;
		uint32_t stride_pixels = b->pitches[0] / 4;

		for (uint32_t y = 0; y < b->height; y++) {
			for (uint32_t x = 0; x < b->width; x++) {
				pix[y * stride_pixels + x] = color;
			}
		}
	}
}


/*
 * add framebuffer from drm_buffer_t
 */
int drm_buf_add_fb(int fd, drm_buffer_t *buf)
{
	return drmModeAddFB2(fd,
							buf->width,
							buf->height,
							buf->format,
							buf->handles,
							buf->pitches,
							buf->offsets,
							&buf->fb_id,
							0);

}

/*
 * atomic modeset with plane
 */
int drm_atomic_modeset_with_plane(vo_unit_t *vo, int fd,
								 uint32_t conn_id,
								 uint32_t crtc_id,
								 uint32_t plane_id,
								 drmModeModeInfo *mode,
								 drm_buffer_t *buf)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	if (!req) {
		LOGE("drmModeAtomicAlloc failed");
		return -ENOMEM;
	}

	/* connector */
	uint32_t prop_conn_crtc = get_prop_id(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

	/* crtc */
	uint32_t prop_crtc_active = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
	uint32_t prop_crtc_mode = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");

	uint32_t mode_blob;
	if (drmModeCreatePropertyBlob(fd, mode,
									sizeof(*mode), &mode_blob)) {
		drmModeAtomicFree(req);
		return -EINVAL;
	}

	/* plane */
	uint32_t prop_plane_crtc = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
	uint32_t prop_plane_fb = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");

	uint32_t prop_crtc_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
	uint32_t prop_crtc_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	uint32_t prop_crtc_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
	uint32_t prop_crtc_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");

	uint32_t prop_src_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
	uint32_t prop_src_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
	uint32_t prop_src_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
	uint32_t prop_src_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");

	/* connector -> crtc */
	drmModeAtomicAddProperty(req, conn_id, prop_conn_crtc, crtc_id);


	/* crtc enable + mode */
	drmModeAtomicAddProperty(req, crtc_id, prop_crtc_active, 1);
	drmModeAtomicAddProperty(req, crtc_id, prop_crtc_mode, mode_blob);

	/* plane setup */
	drmModeAtomicAddProperty(req, plane_id, prop_plane_crtc, crtc_id);
	drmModeAtomicAddProperty(req, plane_id, prop_plane_fb, buf->fb_id);

	drmModeAtomicAddProperty(req, plane_id, prop_crtc_x, 0);
	drmModeAtomicAddProperty(req, plane_id, prop_crtc_y, 0);
	drmModeAtomicAddProperty(req, plane_id, prop_crtc_w, buf->width);
	drmModeAtomicAddProperty(req, plane_id, prop_crtc_h, buf->height);

	drmModeAtomicAddProperty(req, plane_id, prop_src_x, 0);
	drmModeAtomicAddProperty(req, plane_id, prop_src_y, 0);
	drmModeAtomicAddProperty(req, plane_id, prop_src_w, buf->width << 16);
	drmModeAtomicAddProperty(req, plane_id, prop_src_h, buf->height << 16);

	/* commit */
	int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET | \
					DRM_MODE_PAGE_FLIP_EVENT, &(vo->drm_event));
	// int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	if (ret != 0)
	{
		LOGE("drmModeAtomicCommit failed: %s", strerror(errno));
		drmModeAtomicFree(req);
		drmModeDestroyPropertyBlob(fd, mode_blob);
		return -1;
	}


	drmModeAtomicFree(req);
	drmModeDestroyPropertyBlob(fd, mode_blob);

	return ret;
}


int drm_atomic_page_flip(vo_unit_t *vo, int fd, uint32_t plane_id, drm_buffer_t *buf)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();

	uint32_t prop_plane_fb = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");

	drmModeAtomicAddProperty(req, plane_id, prop_plane_fb, buf->fb_id);

	int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, &(vo->drm_event));
	if (ret != 0)
	{
		LOGE("drmModeAtomicCommit failed: %s", strerror(errno));
		drmModeAtomicFree(req);
		return -1;
	}


	drmModeAtomicFree(req);
	return ret;
}


void drm_buf_destroy(int fd, drm_buffer_t *buf)
{
	if( !buf )
		return;

	// remove framebuffer
	if (buf->fb_id) {
		drmModeRmFB(fd, buf->fb_id);
		buf->fb_id = 0;
	}

	if(buf->type == DRM_BUF_DMABUF && buf->gem_imported) {
		LOGI("dma-buf gem handle %u closed...", buf->handles[0]);
		// close the imported gem handle
		drmCloseBufferHandle(fd, buf->handles[0]);

		memset(buf->handles, 0, sizeof(buf->handles));
		buf->gem_imported = false;
		LOGI("dma-buf gem handle %u closed", buf->handles[0]);
	} else {
		// unmap and destroy dumb buffer
		if (buf->map) {
			munmap(buf->map, buf->dumb_size);
			buf->map = NULL;
		}

		if (buf->dumb_handle) {
			struct drm_mode_destroy_dumb dreq = {0};
			dreq.handle = buf->dumb_handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
			buf->dumb_handle = 0;
		}
	}
}

static inline uint64_t ts_to_ms(const struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

static inline uint64_t ts_diff_ms(const struct timespec *end, const struct timespec *start)
{
	return ts_to_ms(end) - ts_to_ms(start);
}

static void vo_drm_page_flip_handler(
	int fd,
	unsigned int frame,
	unsigned int sec,
	unsigned int usec,
	void *data)
{
	vo_drm_event_ctx_t *ctx = data;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	pthread_mutex_lock(&ctx->lock);
	if (ctx->flip_count == 0) {
		ctx->flip_start_ts = now;
	}
	ctx->flip_count++;

	if (ctx->flip_count % 60 == 0) {
		uint64_t ms = ts_diff_ms(&now, &ctx->flip_start_ts);
		double fps = (double)(ctx->flip_count * 1000.0) / (double)ms;
		LOGI("FLIP FPS: %.2f (count=%llu, ms=%llu)", fps, (unsigned long long)ctx->flip_count, (unsigned long long)ms);
	}

	vo_rb_event_t e = {
		.type  = VO_RB_EVT_PAGE_FLIP,
		.frame = frame,
		.ts_ms = ts_to_ms(&now),
	};

	vo_rb_push(&ctx->ring, &e);

	ctx->atomic_committed = true;
	pthread_cond_signal(&ctx->commit_cond);
	pthread_mutex_unlock(&ctx->lock);

	// LOGI("DRM page flip done: frame=%u", frame);
}

static void vo_drm_vblank_handler(
	int fd,
	unsigned int frame,
	unsigned int sec,
	unsigned int usec,
	void *data)
{
	vo_drm_event_ctx_t *ctx = data;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	pthread_mutex_lock(&ctx->lock);

	if (ctx->vblank_count == 0)
		ctx->vblank_start_ts = now;

	ctx->vblank_count++;

	if (ctx->vblank_count % 60 == 0) {
		uint64_t ms = ts_diff_ms(&now, &ctx->vblank_start_ts);
		double fps = (double)ctx->vblank_count * 1000.0 / (double)ms;

		LOGI("VBLANK FPS: %.2f (count=%llu, ms=%llu)",
			 fps,
			 (unsigned long long)ctx->vblank_count,
			 (unsigned long long)ms);
	}

	vo_rb_event_t e = {
		.type  = VO_RB_EVT_VBLANK,
		.frame = frame,
		.ts_ms = ts_to_ms(&now),
	};

	vo_rb_push(&ctx->ring, &e);

	pthread_mutex_unlock(&ctx->lock);

	// LOGI("DRM vsync done: frame=%u", frame);

	/* clrar next vblank */
	drmVBlank vbl = {0};
	vbl.request.type =
		DRM_VBLANK_RELATIVE |
		DRM_VBLANK_EVENT;
	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long)ctx;

	drmWaitVBlank(fd, &vbl);
}

static void *drm_event_thread(void *arg)
{
	// vo_unit_t *vo = arg;
	vo_drm_event_ctx_t *ctx = arg;

	struct pollfd pfd[2] = {
		{
			.fd = ctx->fd,
			.events = POLLIN,
		},
		{
			.fd = ctx->wake_fd,
			.events = POLLIN,
		}
	};

	LOGI("drm_event_thread started");
	while (ctx->running) {
		int ret = poll(pfd, 2, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}

		/* wakeup fd：exit signal */
		if (pfd[1].revents & POLLIN) {
			uint64_t v;
			read(ctx->wake_fd, &v, sizeof(v));
			LOGI("drm_event_thread wakeup exit");
			break;
		}

		/* drm event */
		if (pfd[0].revents & POLLIN) {
			drmHandleEvent(ctx->fd, &ctx->evctx);
		}
	}

	LOGI("drm_event_thread exit");
	return NULL;
}

int vo_drm_event_ctx_init(vo_drm_event_ctx_t *ctx, int drm_fd)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->wake_fd = -1;

	ctx->fd = drm_fd;
	ctx->running = true;
	ctx->atomic_committed = false;

	if (pthread_mutex_init(&ctx->lock, NULL) != 0)
		return -1;
	if (pthread_cond_init(&ctx->commit_cond, NULL) != 0) {
		pthread_mutex_destroy(&ctx->lock);
		return -1;
	}

	memset(&ctx->ring, 0, sizeof(ctx->ring));
	atomic_init(&ctx->ring.widx, 0);
	atomic_init(&ctx->ring.ridx, 0);
	atomic_init(&ctx->ring.data_ready, false);

	ctx->wake_fd = eventfd(0, EFD_NONBLOCK);
	if (ctx->wake_fd < 0) {
		pthread_cond_destroy(&ctx->commit_cond);
		pthread_mutex_destroy(&ctx->lock);
		return -1;
	}

	ctx->evctx.version = DRM_EVENT_CONTEXT_VERSION;
	ctx->evctx.page_flip_handler = vo_drm_page_flip_handler;
	ctx->evctx.vblank_handler = vo_drm_vblank_handler;

	if (pthread_create(&ctx->thread, NULL,
					   drm_event_thread, ctx) != 0) {
		close(ctx->wake_fd);
		ctx->wake_fd = -1;
		pthread_cond_destroy(&ctx->commit_cond);
		pthread_mutex_destroy(&ctx->lock);
		return -1;
	}

	return 0;
}

void vo_drm_event_ctx_deinit(vo_drm_event_ctx_t *ctx)
{
	ctx->running = false;

	if (ctx->wake_fd >= 0) {
		uint64_t v = 1;
		write(ctx->wake_fd, &v, sizeof(v));
	}

	pthread_join(ctx->thread, NULL);

	close(ctx->wake_fd);

	pthread_mutex_destroy(&ctx->lock);
	pthread_cond_destroy(&ctx->commit_cond);
}

int vo_drm_vblank(vo_drm_event_ctx_t *ctx)
{
	drmVBlank vbl = {0};

	if (ctx->vblank_ordered)
		return 0;

	ctx->vblank_count = 0;

	vbl.request.type =
		DRM_VBLANK_RELATIVE |
		DRM_VBLANK_EVENT;

	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long)ctx;

	int ret = drmWaitVBlank(ctx->fd, &vbl);
	if (ret) {
		LOGE("drmWaitVBlank failed: %s", strerror(errno));
		return -1;
	}

	ctx->vblank_ordered = true;
	LOGI("VBLANK Ordered ...");

	return 0;
}