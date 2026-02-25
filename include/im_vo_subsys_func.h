#ifndef __IM_VO_SUBSYS_FUNC_H__
#define __IM_VO_SUBSYS_FUNC_H__

#include "im_vo_helper.h"


typedef struct vo_crc_ops {
	int (*enable)(void);
	int (*dump)(FILE *out);
} vo_crc_ops_t;

typedef struct vo_crc_ctx {
	bool enabled;
	const vo_crc_ops_t *ops;
} vo_crc_ctx_t;


int vo_dump_crc_all(FILE *kmsg);
int vo_polarity_read_all(FILE *kmsg);

#endif // __IM_VO_SUBSYS_FUNC_H__