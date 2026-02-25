#include <stdbool.h>

#include "im_vo_subsys_func.h"


typedef struct vo_crc_desc {
	const char *name;

	uint32_t enable_reg;
	uint32_t enable_val;

	uint32_t crc_reg;

	bool need_enable;
} vo_crc_desc_t;

static vo_crc_desc_t vo_crc_list[] = {
	{
		.name        = "PPI_CRC",
		.enable_reg  = 0x160a2058,
		.enable_val  = 0x01,
		.crc_reg     = 0x160a2038,
		.need_enable = true,
	},
	{
		.name        = "EDPI_CRC",
		.crc_reg     = 0x1604001c,
		.need_enable = false,
	},
};


static int vo_crc_enable(const vo_crc_desc_t *desc, FILE *kmsg)
{
	if (!desc->need_enable)
		return 0;

	if (vo_write_reg32(desc->enable_reg, desc->enable_val) == 0) {
		// fprintf(kmsg,
		//         "[vo_unit] %s enable:0x%08x\n",
		//         desc->name,
		//         desc->enable_val);
		return 0;
	}

	fprintf(kmsg,
			"[vo_unit] %s enable failed (0x%08x)\n",
			desc->name,
			desc->enable_reg);
	return -1;
}


static int vo_crc_read(const vo_crc_desc_t *desc, FILE *kmsg)
{
	uint32_t val;

	if (vo_read_reg32(desc->crc_reg, &val) == 0) {
		fprintf(kmsg,
				"[vo_unit] %s:0x%08x\n",
				desc->name,
				val);
		return 0;
	}

	fprintf(kmsg,
			"[vo_unit] %s read failed (0x%08x)\n",
			desc->name,
			desc->crc_reg);
	return -1;
}

int vo_dump_crc_all(FILE *kmsg)
{
	int i;
	int cnt = sizeof(vo_crc_list) / sizeof(vo_crc_list[0]);

	for (i = 0; i < cnt; i++) {
		vo_crc_enable(&vo_crc_list[i], kmsg);
	}

	/* monitor / pipeline ready delay */
	sleep(2);

	for (i = 0; i < cnt; i++) {
		vo_crc_read(&vo_crc_list[i], kmsg);
	}

	return 0;
}


int vo_crc_ctx_init(vo_crc_ctx_t *ctx, const vo_crc_ops_t *ops)
{
    ctx->ops = ops;
    ctx->enabled = false;
    return 0;
}

int vo_crc_ctx_dump(vo_crc_ctx_t *ctx, FILE *out)
{
    if (!ctx->enabled && ctx->ops->enable) {
        ctx->ops->enable();
        ctx->enabled = true;
    }

    return ctx->ops->dump(out);
}


// static const vo_crc_ops_t vo_crc_ops = {
// 	.enable = vo_crc_enable,
// 	.dump = vo_dump_crc_all,
// };
