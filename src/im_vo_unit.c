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
#include <signal.h>

#include "im_vo_unit.h"
#include "im_vo_subsys_func.h"

#define __TAG__ "vo_unit"

vo_unit_t vo_unit;


static const char *vo_pix_fmt_name[] = {
	"PIX_FMT_ARGB8888",
	"PIX_FMT_ABGR8888",
	"PIX_FMT_RGBA8888",
	"PIX_FMT_BGRA8888",
	"PIX_FMT_XRGB8888",
	"PIX_FMT_RGB888",
	"PIX_FMT_BGR888",
	"PIX_FMT_YUV420_8_SP",
	"PIX_FMT_YUV422_8_PACK",
	"PIX_FMT_RGBA1010102",
	"PIX_FMT_ARGB2101010",
	"PIX_FMT_INVALID",
};


static uint32_t pixfmt_to_im_pixfmt(vo_pix_fmt_t fmt)
{
	switch (fmt) {
		case PIX_FMT_ARGB8888: return IM_PIX_FMT_ARGB8888;
		case PIX_FMT_ABGR8888: return IM_PIX_FMT_ABGR8888;
		case PIX_FMT_RGBA8888: return IM_PIX_FMT_RGBA8888;
		case PIX_FMT_BGRA8888: return IM_PIX_FMT_BGRA8888;
		case PIX_FMT_XRGB8888: return IM_PIX_FMT_XRGB8888;
		case PIX_FMT_RGB888:   return IM_PIX_FMT_RGB888;
		case PIX_FMT_BGR888:   return IM_PIX_FMT_BGR888;
		case PIX_FMT_YUV420_8_SP: return IM_PIX_FMT_YUV420_8_SP;
		case PIX_FMT_YUV422_8_PACK: return IM_PIX_FMT_YUV422_8_PACK;   // YUYV,  1 Plane, [31:24][23:16][15: 8][ 7: 0]
		case PIX_FMT_RGBA1010102: return IM_PIX_FMT_RGBA1010102;
		case PIX_FMT_ARGB2101010: return IM_PIX_FMT_ARGB2101010;
		default: return PIX_FMT_INVALID;
	}
}

static void print_format_index(void)
{
	for (int i = 0; i < PIX_FMT_INVALID; i++) {
		LOGI("%d : %s", i, vo_pix_fmt_name[i]);
	}
}


static void usage(void)
{
	LOGI("Usage: vo_unit -m mode -w width -h height");
	LOGI("  -m mode: 1: dma-buf 0: dumb buf\n");
	LOGI("  -w width: input image width");
	LOGI("  -h height: input image height");
	LOGI("  -a hsize_align: input image hsize alignment, default 4 (align to 16 bytes)");
	LOGI("  -f format: input image pixel format index");
	LOGI("  -V vsync: vsync count");
	LOGI("  -i input_file: input image file name");
	LOGI("  -r vrefresh: crtc vrefresh");
	LOGI("Pixel Format Index:");
	print_format_index();
}

static int parse_parameter(int argc, char **argv)
{
	int opt = 0;
	int mode = -1;
	int width = -1;
	int height = -1;
	int vrefresh= -1;
	int hsize_align = 4;
	int format = -1;
	int test_vblank = 0;

	if(argc < 2) {
		usage();
		return -1;
	}

	while ((opt = getopt(argc, argv, "m:g:w:h:a:f:i:r:V:")) != -1) {

		switch (opt)
		{
		case 'm':
			mode = atoi(optarg); // mode:0 dumb buffer, 1 dma buffer
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'r':
			vrefresh = atoi(optarg);
			break;
		case 'a':
			hsize_align = atoi(optarg);
			break;
		case 'f':
			format = atoi(optarg);
			break;
		case 'i':
			snprintf(vo_unit.file_name, sizeof(vo_unit.file_name), "%s", optarg);
			break;
		case 'V':
			test_vblank = atoi(optarg); // test vblank
			break;
		default:
			usage();
			return -1;
		}
	}

	vo_unit.mode = mode;
	vo_unit.width = width;
	vo_unit.height = height;
	vo_unit.vrefresh = vrefresh;
	vo_unit.hsize_align = hsize_align;
	vo_unit.format = format;
	vo_unit.test_vblank = test_vblank;

	LOGI("vo_unit parameters: mode=%d, width=%d, height=%d, hsize_align=%d, format=%d, file_name=%s",
			vo_unit.mode, vo_unit.width, vo_unit.height,
			vo_unit.hsize_align, vo_unit.format, vo_unit.file_name);

	return 0;
}

static int load_image_to_fb(vo_unit_t *vo, im_fb_t **out_fb)
{
	FILE *fp = NULL;
	im_fb_t *fb = NULL; // output
	im_fb_attr_t fb_attr = {0}; // im_hal_fb_alloc input
	im_frame_info_t fb_info = {0}; // input
	im_size_t mem_size;

	const char *file_name = vo->file_name;

	if (file_name && strlen(file_name) == 0) {
		LOGI("No input file specified, try open default frame0_lpu0_l0_1080x1920_XR24.bin");
		snprintf(vo_unit.file_name, sizeof(vo_unit.file_name), "%s", "./raw_dump/frame0_lpu0_l0_1080x1920_XR24.bin");
	}

	if (vo->width <= 0 || vo->height <= 0) {
		return -1;
	}

	fb_info.width = vo->width;
	fb_info.height = vo->height;
	fb_info.format.pix_fmt = pixfmt_to_im_pixfmt(vo->format); // e.g., DRM_FORMAT_XRGB8888

	fb_attr.buf_num = 1;
	fb_attr.name = "vo_demo_fb";
	fb_attr.heap_id = IM_MEM_HEAP_MEDIA;
	fb_attr.norm.hsize_align = vo->hsize_align;

	fb = im_hal_fb_alloc(&fb_info, &fb_attr);
	if (!fb) {
		LOGE("Alloc fb  failed: width=%d, height=%d, format=%d",
			fb_info.width, fb_info.height, fb_info.format.pix_fmt);
		LOGI("fb_attr: buf_num=%d, name=%s, heap_id=%d",
			fb_attr.buf_num, fb_attr.name, fb_attr.heap_id);
		return -1;
	}
	LOGI("planes: nplanes=%d pix_fmt: 0x%x, ", fb->nplanes, fb->info.format.pix_fmt);

	mem_size = im_hal_fb_get_size(fb);

	fp = fopen(file_name, "rb");
	if (!fp) {
		LOGE("failed to open image file: %s", file_name);
		return -1;
	}

	im_hal_mem_sync_start_with_flag(fb->mem_handle, IM_MEM_SYNC_RW);
	size_t read_size = fread(im_hal_mem_map(fb->mem_handle), 1, mem_size, fp);
	im_hal_mem_unmap(fb->mem_handle);
	im_hal_mem_sync_end_with_flag(fb->mem_handle, IM_MEM_SYNC_RW);
	fclose(fp);

	if (read_size != mem_size) {
		LOGI("fread mismatch: expect %d, got %zu", mem_size, read_size);
		return -1;
	}
	LOGI("Image data loaded to framebuffer from file: %s", file_name);

	*out_fb = fb;
	return 0;
}

// Signal handler for SIGINT
void handle_sigint(int sig)
{
	LOGI("Caught signal %d, exiting...", sig);
	vo_unit.exiting = true;
}

void *data_dump_thread(void *arg)
{
	vo_drm_event_ctx_t *ctx = arg;
	vo_unit_t *vo = container_of(ctx, vo_unit_t, drm_event);
	FILE *kmsg;
	vo_rb_event_t e;
	int intr_count = 0;

	LOGI("data_dump_thread started");
	pthread_mutex_lock(&ctx->lock);
	while (!ctx->atomic_committed && ctx->running) {
		pthread_cond_wait(&ctx->commit_cond, &ctx->lock);
		// avoid thread wake up continue
		if (!ctx->running)
			break;
	}
	pthread_mutex_unlock(&ctx->lock);


	if (!ctx->running)
		return NULL;

	kmsg = fopen("/dev/kmsg", "w");
	if (!kmsg) {
		perror("open /dev/kmsg");
		return NULL;
	}

	vo_dump_crc_all(kmsg);
	vo_polarity_read_all(kmsg);

	/* NEW: ringbuf log */
	while (ctx->running) {
		if (!atomic_load_explicit(&ctx->ring.data_ready,
									memory_order_acquire)) {
			usleep(1000);
			continue;
		}
		break;
	}

	while (vo_rb_pop(&ctx->ring, &e)) {
		if (e.type == VO_RB_EVT_VBLANK) {
			fprintf(kmsg,
				"[vo_unit] VBLANK: frame=%u ts=%lu\n",
				e.frame, e.ts_ms);
			intr_count++;
		}

		if (e.type == VO_RB_EVT_PAGE_FLIP){
			fprintf(kmsg,
				"[vo_unit] FLIP:   frame=%u ts=%lu\n",
				e.frame, e.ts_ms);
			intr_count++;
		}
		if(vo->test_vblank) {
			if (intr_count == 2)
				break;
		} else {
			if (intr_count == 1)
				break;
		}
	}

	fflush(kmsg);
	atomic_store_explicit(&ctx->ring.data_ready,
							false, memory_order_release);

	fclose(kmsg);

	vo_notify_display_status(VO_NOTIFY_DISPLAY_STARTED);

	LOGI("data_dump_thread data dump done, then exit");

	return NULL;
}

int main(int argc, char **argv)
{
	int ret;
	int cur = 0;
	im_fb_t *fb = NULL;
	drm_buffer_t buf[2];
	drmModeModeInfo mode;

	memset(&vo_unit, 0, sizeof(vo_unit_t));
	vo_drm_event_ctx_t *event_ctx = &(vo_unit.drm_event);

	if (parse_parameter(argc, argv) < 0) {
		exit(1);
	}

	ret = vo_notify_init();
	if (ret < 0) {
		LOGE("crate notify failed");
		goto clean_lv0;
	}

	// Register the signal handler
	signal(SIGINT, handle_sigint);


	vo_unit.drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (vo_unit.drm_fd < 0) {
		LOGE("failed to open DRM device");
		goto clean_lv0;
	}

	vo_unit.exiting = false;

	ret = vo_drm_event_ctx_init(event_ctx, vo_unit.drm_fd);
	if(ret < 0) {
		LOGE("drm event ctx init failed");
		goto clean_lv1;
	}

	// Start the data dump thread
	if (pthread_create(&vo_unit.dump_thread, NULL, data_dump_thread, event_ctx) != 0) {
		LOGE("failed to create data dump thread");
		goto clean_lv2;
	}

	drmSetClientCap(vo_unit.drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(vo_unit.drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);

	ret = find_connector_crtc_plane(&vo_unit);
	if (ret < 0) {
		LOGE("find_connector_crtc_plane failed");
		goto clean_lv3;
	}

	mode = vo_unit.drm_mode;
	LOGI("Using connector %u, crtc %u, plane %u, hdisplay %u, vdisplay %u, vrefresh %u", \
		vo_unit.conn_id, vo_unit.crtc_id, vo_unit.plane_id, mode.hdisplay, mode.vdisplay, mode.vrefresh);

	if (vo_unit.mode == 1) {

		if (load_image_to_fb(&vo_unit, &fb) < 0) {
			LOGE("load_image_to_fb failed");
			goto clean_lv3;
		}

		/* DMA-BUF -> drm_buffer_t */
		ret = drm_display_from_dmabuf(vo_unit.drm_fd, fb, &buf[0]);
		if (ret < 0) {
			LOGE("drm_display_from_dmabuf failed");
			goto clean_lv4;
		}

		/* add framebuffer */
		ret = drm_buf_add_fb(vo_unit.drm_fd, &buf[0]);
		if (ret < 0) {
			LOGE("drm_buf_add_fb (dma) failed");
			goto clean_lv4;
		}

		LOGI("DMA-BUF framebuffer created: fb_id=%u", buf[0].fb_id);

	} else {

		ret = drm_buf_create_dumb(vo_unit.drm_fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, &buf[0]);
		if (ret < 0) {
			LOGE("drm_buf_create_dumb for buf[0] failed");
			goto clean_lv4;
		}
		ret = drm_buf_create_dumb(vo_unit.drm_fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, &buf[1]);
		if (ret < 0) {
			LOGE("drm_buf_create_dumb for buf[1] failed");
			goto clean_lv4;
		}
		drm_buf_fill_color(&buf[0], 0x00FF0000);
		drm_buf_fill_color(&buf[1], 0x000000FF);
		ret = drm_buf_add_fb(vo_unit.drm_fd, &buf[0]);
		if (ret < 0) {
			LOGE("drm_buf_add_fb for buf[0] failed");
			goto clean_lv4;
		}
		ret = drm_buf_add_fb(vo_unit.drm_fd, &buf[1]);
		if (ret < 0) {
			LOGE("drm_buf_add_fb for buf[1] failed");
			goto clean_lv4;
		}
		LOGI("Dumb buffers created and filled");
	}

	/*
	* atomic modeset with plane
	*/
	drm_atomic_modeset_with_plane(&vo_unit,
		vo_unit.drm_fd,
		vo_unit.conn_id,
		vo_unit.crtc_id,
		vo_unit.plane_id,
		&mode,
		&buf[0]);
	LOGI("Atomic modeset with plane done");
	// getchar();

	if(vo_unit.test_vblank)
		vo_drm_vblank(event_ctx); // vblank event request

	while (!vo_unit.exiting) {
		if (vo_unit.mode == 0) {
			drm_atomic_page_flip(&vo_unit, vo_unit.drm_fd, vo_unit.plane_id, &buf[cur]);
			cur ^= 1;
			// usleep(2000000); // 2s
		} else {
			// sleep(1);
			drm_atomic_page_flip(&vo_unit, vo_unit.drm_fd, vo_unit.plane_id, &buf[0]);
		}
	}

clean_lv4:
	/* Cleanup resources */
	if (vo_unit.mode == 1) {
		drm_buf_destroy(vo_unit.drm_fd, &buf[0]);

		if (fb)
			im_hal_fb_free(fb);
	} else {
		drm_buf_destroy(vo_unit.drm_fd, &buf[0]);
		drm_buf_destroy(vo_unit.drm_fd, &buf[1]);
	}

	if (vo_notify_display_status(VO_NOTIFY_DISPLAY_STOPPED) < 0) {
		LOGE("VO_NOTIFY_DISPLAY_STOPPED failed");
	}


clean_lv3:
    // Signal dump thread to exit if it is waiting
    pthread_mutex_lock(&event_ctx->lock);
    event_ctx->running = false;
    pthread_cond_signal(&event_ctx->commit_cond);
    pthread_mutex_unlock(&event_ctx->lock);

	if(vo_unit.dump_thread)
		pthread_join(vo_unit.dump_thread, NULL);

clean_lv2:
	vo_drm_event_ctx_deinit(event_ctx);

clean_lv1:
	close(vo_unit.drm_fd);
	LOGI("Program exited cleanly");

clean_lv0:

	return 0;
}
