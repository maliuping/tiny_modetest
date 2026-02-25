#include <stdbool.h>

#include "im_vo_subsys_func.h"


typedef struct vo_polarity_desc {
	const char *name;

	uint32_t enable_reg;
	uint32_t enable_val;

	uint32_t reg;

	bool need_enable;
} vo_polarity_desc_t;

static vo_polarity_desc_t vo_polarity_list[] = {
	{
		.name        = "DPU_POLARITY",
		.reg         = 0x16001eec,
		.need_enable = false,
	},
	{
		.name        = "DSI_POLARITY",
		.reg         = 0x160a0014,
		.need_enable = false,
	},
};


static int vo_polarity_read(const vo_polarity_desc_t *desc, FILE *kmsg)
{
	uint32_t val;

	if (vo_read_reg32(desc->reg, &val) == 0) {
		fprintf(kmsg,
				"[vo_unit] %s:0x%08x\n",
				desc->name,
				val);
		return 0;
	}

	fprintf(kmsg,
			"[vo_unit] %s read failed (0x%08x)\n",
			desc->name,
			desc->reg);
	return -1;
}

int vo_polarity_read_all(FILE *kmsg)
{
	int i;
	int desc_count = sizeof(vo_polarity_list) / sizeof(vo_polarity_desc_t);

	for (i = 0; i < desc_count; i++) {
		vo_polarity_read(&vo_polarity_list[i], kmsg);
	}

	return 0;
}