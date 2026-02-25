#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "im_vo_helper.h"
#include "im_vo_unit.h"

#define __TAG__ "vo_helper"

#define VO_NOTIFY_FILE "/tmp/vo_ready"

#define MAP_SIZE   0x1000
#define MAP_MASK   (MAP_SIZE - 1)


static const char *vo_msg[] = {
	"DISPLAY_STARTED\n",
	"DISPLAY_STOPPED\n",
	"INVALID_MESSAGE\n"
};


static const char *vo_status_to_msg(vo_notify_msg_t status)
{
	if (status < 0 || status >= VO_NOTIFY_MSG_MAX)
		return vo_msg[VO_NOTIFY_INVALID_MESSAGE];

	return vo_msg[status];
}

int vo_read_reg32(uint64_t phys_addr, uint32_t *out_val)
{
	int fd;
	void *map_base;
	volatile uint32_t *reg;

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("open /dev/mem");
		return -1;
	}

	map_base = mmap(NULL,
					MAP_SIZE,
					PROT_READ,
					MAP_SHARED,
					fd,
					phys_addr & ~MAP_MASK);
	if (map_base == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return -1;
	}

	reg = (volatile uint32_t *)((char *)map_base +
								(phys_addr & MAP_MASK));
	*out_val = *reg;

	munmap(map_base, MAP_SIZE);
	close(fd);
	return 0;
}

int vo_write_reg32(uint64_t phys_addr, uint32_t value)
{
	int fd;
	void *map_base;
	volatile uint32_t *reg;

	// open /dev/mem
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open /dev/mem");
		return -1;
	}

	// map physical address to virtual address
	map_base = mmap(NULL,
					MAP_SIZE,
					PROT_READ | PROT_WRITE,
					MAP_SHARED,
					fd,
					phys_addr & ~MAP_MASK);
	if (map_base == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return -1;
	}

	// compute register vir address
	reg = (volatile uint32_t *)((char *)map_base +
								(phys_addr & MAP_MASK));

	// write value to register
	*reg = value;

	// unmap and close
	munmap(map_base, MAP_SIZE);
	close(fd);

	return 0;
}

int vo_notify_init(void)
{
	int fd;

	fd = open(VO_NOTIFY_FILE, O_WRONLY | O_CREAT, 0666);
	if (fd < 0) {
		perror("open " VO_NOTIFY_FILE);
		return -1;
	}

	close(fd);
	return 0;
}

int vo_notify_display_status(vo_notify_msg_t status)
{
	int fd;
	const char *msg = vo_status_to_msg(status);

	fd = open(VO_NOTIFY_FILE, O_WRONLY | O_TRUNC);
	if (fd < 0) {
		perror("open " VO_NOTIFY_FILE);
		return -1;
	}

	if (write(fd, msg, strlen(msg)) < 0) {
		perror("write " VO_NOTIFY_FILE);
		close(fd);
		return -1;
	}

	close(fd);

	LOGI("VO display status updated: %s", msg);
	return 0;
}


void vo_notify_deinit(void)
{
	/* unlink(VO_NOTIFY_FIFO); */
}



void vo_rb_push(vo_ringbuf_t *rb, vo_rb_event_t *e)
{
	unsigned int w =
	atomic_fetch_add_explicit(&rb->widx, 1,
			memory_order_relaxed);

	rb->buf[w % VO_RB_SIZE] = *e;

	atomic_store_explicit(&rb->data_ready, true,
	memory_order_release);
}

bool vo_rb_pop(vo_ringbuf_t *rb, vo_rb_event_t *out)
{
	unsigned int r =
	atomic_load_explicit(&rb->ridx,
		memory_order_relaxed);
	unsigned int w =
	atomic_load_explicit(&rb->widx,
		memory_order_acquire);

	if (r == w)
	return false;

	*out = rb->buf[r % VO_RB_SIZE];
	atomic_store_explicit(&rb->ridx, r + 1,
	memory_order_relaxed);

	return true;
}
