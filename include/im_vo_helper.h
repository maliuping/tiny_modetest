#ifndef __IM_VO_HELPER_H__
#define __IM_VO_HELPER_H__

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))


#define VO_RB_SIZE 1024

typedef enum vo_notify_msg {
	VO_NOTIFY_DISPLAY_STARTED = 0,
	VO_NOTIFY_DISPLAY_STOPPED,
	VO_NOTIFY_INVALID_MESSAGE,
	VO_NOTIFY_MSG_MAX,
} vo_notify_msg_t;


typedef enum {
	VO_RB_EVT_VBLANK,
	VO_RB_EVT_PAGE_FLIP,
} vo_rb_evt_t;

typedef struct {
	vo_rb_evt_t type;
	uint64_t    ts_ms;
	uint32_t    frame;
} vo_rb_event_t;

typedef struct {
	vo_rb_event_t buf[VO_RB_SIZE];
	atomic_uint   widx;
	atomic_uint   ridx;
	atomic_bool   data_ready;
} vo_ringbuf_t;


int vo_read_reg32(uint64_t phys_addr, uint32_t *out_val);
int vo_write_reg32(uint64_t phys_addr, uint32_t value);

int vo_notify_init(void);
int vo_notify_display_status(vo_notify_msg_t status);
void vo_notify_deinit(void);

void vo_rb_push(vo_ringbuf_t *rb, vo_rb_event_t *e);
bool vo_rb_pop(vo_ringbuf_t *rb, vo_rb_event_t *out);
#endif // __IM_VO_HELPER_H