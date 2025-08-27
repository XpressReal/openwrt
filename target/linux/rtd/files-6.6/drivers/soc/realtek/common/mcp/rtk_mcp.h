/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 */
#ifndef __REALTEK_MCP_H
#define __REALTEK_MCP_H

#include <linux/types.h>
#include <linux/rbtree.h>

struct mcp_device_data;

struct mcp_dma_buf {
	struct rb_node              rb_node;
	struct dma_buf_attachment   *attachment;
	struct sg_table             *sgt;
	dma_addr_t                  dma_addr;
	unsigned int                dma_len;
};

extern struct platform_driver rtk_mcp_driver;

int mcp_do_command(struct mcp_device_data *data, struct mcp_desc *descs, int n);
int mcp_import_buf(struct mcp_device_data *data, struct mcp_dma_buf *mcp_buf, int fd);
void mcp_release_buf(struct mcp_dma_buf *mcp_buf);
void mcp_set_auto_padding(struct mcp_device_data *data, int endis);

int smcp_encrypt_image_with_ik(u32 data_in, u32 data_out, u32 length);
int smcp_decrypt_image_with_ik(u32 data_in, u32 data_out, u32 length);


#endif
