// SPDX-License-Identifier: GPL-2.0-only
#include "hse.h"
#include "uapi/hse.h"

static int __append_copy(struct hse_command_queue *cq, u32 dst, u32 src, u32 size, u64 flags)
{
	u32 cmds[4];
	u32 swap_opt = 0;

	flags &= HSE_FLAGS_COPY_VALID_MASK;
	if (flags & HSE_FLAGS_COPY_SWAP_EN)
		swap_opt |= BIT(8) | ((flags & HSE_FLAGS_COPY_SWAP_OPT_MASK) << 9);

	cmds[0] = 0x1 | swap_opt;
	cmds[1] = size;
	cmds[2] = dst;
	cmds[3] = src;
	return hse_cq_add_data(cq, cmds, 4);
}

static int __append_picture_copy(struct hse_command_queue *cq, u32 dst, u16 dst_pitch,
				       u32 src, u16 src_pitch, u16 width, u16 height, u64 flags)
{
	u32 cmds[5];
	u32 swap_opt = 0;

	flags &= HSE_FLAGS_COPY_VALID_MASK;
	if (flags & HSE_FLAGS_COPY_SWAP_EN)
		swap_opt |= BIT(8) | ((flags & HSE_FLAGS_COPY_SWAP_OPT_MASK) << 9);

	cmds[0] = 0x1 | swap_opt | BIT(14);
	cmds[1] = height | (width << 16);
	cmds[2] = dst;
	cmds[3] = src;
	cmds[4] = dst_pitch | (src_pitch << 16);
	return hse_cq_add_data(cq, cmds, 5);
}

static int __append_constant_fill(struct hse_command_queue *cq, u32 dst, u32 val, u32 size)
{
	u32 cmds[4];

	cmds[0] = 0x1 | BIT(19);
	cmds[1] = size;
	cmds[2] = dst;
	cmds[3] = val;
	return hse_cq_add_data(cq, cmds, 4);
}

static int __append_xor(struct hse_command_queue *cq, u32 dst, u32 *src, u32 src_cnt, u32 size)
{
	u32 cmds[8];
	int i;

	cmds[0] = 0x1 | ((src_cnt - 1) << 16);
	cmds[1] = size;
	cmds[2] = dst;
	for (i = 0; i < src_cnt; i++)
		cmds[i + 3] = src[i];
	return hse_cq_add_data(cq, cmds, i + 3);
}

static int __append_yuy2_to_nv16(struct hse_command_queue *cq, u32 luma, u32 chroma, u16 dst_pitch,
					u32 src, u32 src_pitch, u16 width, u16 height, u64 flags)
{
	u32 cmds[6];

	cmds[0] = 0x2;
	cmds[1] = height | (width << 16);
	cmds[2] = dst_pitch | (src_pitch << 16);
	cmds[3] = luma;
	cmds[4] = chroma;
	cmds[5] = src;
	return hse_cq_add_data(cq, cmds, 6);
}

static const u32 swap_opt_rorn[][4] = {
	{  0, 18, 16,  9 },
	{  1, 12, 22, 11 },
	{  2, 19, 10, 15 },
	{  3,  6, 20, 17 },
	{  4, 13,  8, 21 },
	{  5,  7, 14, 23 },
	{  6, 20, 17,  3 },
	{  7, 14, 23,  5 },
	{  8, 21,  4, 13 },
	{  9,  0, 18, 16 },
	{ 10, 15,  2, 19 },
	{ 11,  1, 12, 22 },
	{ 12, 22, 11,  1 },
	{ 13,  8, 21,  4 },
	{ 14, 23,  5,  7 },
	{ 15,  2, 19, 10 },
	{ 16,  9,  0, 18 },
	{ 17,  3,  6, 20 },
	{ 18, 16,  9,  0 },
	{ 19, 10, 15,  2 },
	{ 20, 17,  3,  6 },
	{ 21,  4, 13,  8 },
	{ 22, 11,  1, 12 },
	{ 23,  5,  7, 14 },
};

#define COPY_SIZE_MAX    (518144)

static int hse_cq_prep_copy_f(struct hse_device *hse_dev, struct hse_command_queue *cq,
			      dma_addr_t dst, dma_addr_t src, u32 size, u64 flags)
{
	u32 c_size = size;
	u32 c_dst_addr = dst;
	u32 c_src_addr = src;
	int ret;
	int should_split = (c_src_addr % 16) || (c_dst_addr % 16) || (c_size % 16);

	if (flags)
		return -EINVAL;

	while (should_split && c_size > COPY_SIZE_MAX) {
		ret = __append_copy(cq, c_dst_addr, c_src_addr, COPY_SIZE_MAX, 0);
		if (ret)
			return ret;
		c_size -= COPY_SIZE_MAX;
		c_dst_addr += COPY_SIZE_MAX;
		c_src_addr += COPY_SIZE_MAX;
	}

	should_split = should_split && (c_size > 16);

	if (should_split) {
		u32 new_size;

		if (c_size <= 0x20)
			new_size = 0x10;
		else if (c_size <= 0x800)
			new_size = 0x20;
		else
			new_size = 0x800;

		ret = __append_copy(cq, c_dst_addr, c_src_addr, c_size & ~(new_size - 1), 0);
		if (ret)
			return ret;
		ret = __append_copy(cq, c_dst_addr + c_size - new_size,
				  c_src_addr + c_size - new_size, new_size, 0);
		if (ret)
			return ret;
		c_size = 0;
	}

	if (c_size != 0)
		ret = __append_copy(cq, c_dst_addr, c_src_addr, c_size, 0);
	return ret;
}

int hse_cq_prep_copy(struct hse_device *hse_dev, struct hse_command_queue *cq,
		     dma_addr_t dst, dma_addr_t src, u32 size, u64 flags)
{
	if (hse_should_workaround_copy(hse_dev))
		return hse_cq_prep_copy_f(hse_dev, cq, dst, src, size, flags);

	return __append_copy(cq, dst, src, size, flags);
}

int hse_cq_prep_copy_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
			struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
			struct scatterlist *src_sg, u32 src_nents, u32 src_offset,
			u32 size, u64 flags)
{
	dma_addr_t dst_addr, src_addr;
	u32 i = 0, j = 0;
	u32 dst_len, src_len;
	u32 chunk_size;
	int ret;

	if (hse_should_workaround_copy(hse_dev) || flags)
		return -EINVAL;

	while (i < dst_nents && j < src_nents && size > 0) {
		dst_len = sg_dma_len(dst_sg);
		if (dst_offset >= dst_len) {
			dst_offset -= dst_len;
			dst_sg = sg_next(dst_sg);
			i += 1;
			continue;
		}
		dst_addr = sg_dma_address(dst_sg) + dst_offset;

		src_len = sg_dma_len(src_sg);
		if (src_offset >= src_len) {
			src_offset -= src_len;
			src_sg = sg_next(src_sg);
			j += 1;
			continue;
		}
		src_addr = sg_dma_address(src_sg) + src_offset;

		chunk_size = min3(dst_len - dst_offset, src_len - src_offset, size);

		ret = __append_copy(cq, dst_addr, src_addr, chunk_size, 0);
		if (ret)
			return ret;

		dst_offset += chunk_size;
		src_offset += chunk_size;
		size -= chunk_size;
	}

	return size != 0 ? -EINVAL : 0;
}

static bool picture_copy_args_valid(u32 dst, u16 dst_pitch, u32 src, u16 src_pitch, u16 width, u16 height)
{
	return IS_ALIGNED(dst, 4) && IS_ALIGNED(dst_pitch, 4) &&
		IS_ALIGNED(src, 4) && IS_ALIGNED(src_pitch, 4) &&
		IS_ALIGNED(width, 4) && dst_pitch >= width && src_pitch >= width;
}

int hse_cq_prep_picture_copy(struct hse_device *hse_dev, struct hse_command_queue *cq,
			     dma_addr_t dst, u16 dst_pitch, dma_addr_t src, u16 src_pitch,
			     u16 width, u16 height, u64 flags)
{
	if (!picture_copy_args_valid(dst, dst_pitch, src, src_pitch, width, height))
		return -EINVAL;

	return __append_picture_copy(cq, dst, dst_pitch, src, src_pitch, width, height, flags);
}

int hse_cq_prep_picture_copy_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset, u16 dst_pitch,
				struct scatterlist *src_sg, u32 src_nents, u32 src_offset, u16 src_pitch,
				u16 width, u16 height, u64 flags)
{
	u32 dst_addr, src_addr;
	u32 i = 0, j = 0;
	u32 dst_len, src_len;
	u32 size, left = width;
	u32 max_pitch = max(dst_pitch, src_pitch);
	int ret;

	if (hse_should_workaround_copy(hse_dev))
		return -EINVAL;

	if (!picture_copy_args_valid(dst_offset, dst_pitch, src_offset, src_pitch, width, height))
		return -EINVAL;

	while (i < dst_nents && j < src_nents && height > 0) {
		dst_len = sg_dma_len(dst_sg);
		if (dst_offset >= dst_len) {
			dst_offset -= dst_len;
			dst_sg = sg_next(dst_sg);
			i += 1;
			continue;
		}

		src_len = sg_dma_len(src_sg);
		if (src_offset >= src_len) {
			src_offset -= src_len;
			src_sg = sg_next(src_sg);
			j += 1;
			continue;
		}

		dst_addr = sg_dma_address(dst_sg) + dst_offset;
		src_addr = sg_dma_address(src_sg) + src_offset;

		size = min(dst_len - dst_offset, src_len - src_offset);
		if (size >= max_pitch && left == width) {
			u32 h = min((u32)height, size / max_pitch);

			ret = __append_picture_copy(cq, dst_addr, dst_pitch, src_addr, src_pitch,
						    width, h, flags);
			if (ret)
				return ret;
			dst_offset += dst_pitch * h;
			src_offset += src_pitch * h;
			height -= h;
		} else {
			size = min(size, left);

			ret = __append_copy(cq, dst_addr, src_addr, size, flags);
			if (ret)
				return ret;

			dst_offset += size;
			src_offset += size;
			left -= size;

			if (left == 0) {
				left = width;
				dst_offset += (dst_pitch - width);
				src_offset += (src_pitch - width);
				height -= 1;
			}
		}
	}

	return height != 0 ? -EINVAL : 0;
}

static int hse_cq_prep_constant_fill_f(struct hse_device *hse_dev, struct hse_command_queue *cq,
				       dma_addr_t dst, u32 val, u32 size, u64 flags)
{
	u32 split_size = 16777216;
	u32 c_dst_addr = dst;
	u32 c_size = size;
	int ret;
	u32 val_in;

	if (size < 32)
		return -EINVAL;

	while (c_size >= 64) {
		if (c_size < split_size) {
			split_size = split_size == 8192 ? 2048 : split_size / 2;
			continue;
		}

		ret = __append_constant_fill(cq, c_dst_addr, val, split_size);
		if (ret)
			return ret;

		c_dst_addr += split_size;
		c_size     -= split_size;
	}

	if (c_size >= 32)
		return __append_constant_fill(cq, c_dst_addr, val, c_size);

	if (c_size == 0)
		return 0;

	/* no swap_opt in constant_fill cmd  */
	val_in = ror32(val, (c_size & 0x3) * 8);
	return __append_constant_fill(cq, c_dst_addr + c_size - 32,
					   val_in, 32);
}

int hse_cq_prep_constant_fill(struct hse_device *hse_dev, struct hse_command_queue *cq,
			      dma_addr_t dst, u32 val, u32 size, u64 flags)
{
	if (hse_should_workaround_copy(hse_dev))
		return hse_cq_prep_constant_fill_f(hse_dev, cq, dst, val, size, flags);

	return __append_constant_fill(cq, dst, val, size);
}

int hse_cq_prep_constant_fill_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				 struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
				 u32 val, u32 size, u64 flags)
{
	u32 dst_addr;
	u32 i = 0;
	u32 dst_len;
	u32 chunk_size;
	int ret;
	u32 left = size;
	u32 val_in;

	if (hse_should_workaround_copy(hse_dev))
		return -EINVAL;

	while (i < dst_nents && left > 0) {
		dst_len = sg_dma_len(dst_sg);
		if (dst_offset >= dst_len) {
			dst_offset -= dst_len;
			dst_sg = sg_next(dst_sg);
			i += 1;
			continue;
		}
		dst_addr = sg_dma_address(dst_sg) + dst_offset;
		chunk_size = min(dst_len - dst_offset, left);

		/* no swap_opt in constant_fill cmd  */
		val_in = ror32(val, ((size - left) & 0x3) * 8);

		ret = __append_constant_fill(cq, dst_addr, val_in, chunk_size);
		if (ret)
			return ret;

		dst_offset += chunk_size;
		left -= chunk_size;
	}

	return left != 0 ? -EINVAL : 0;
}

int hse_cq_prep_xor(struct hse_device *hse_dev, struct hse_command_queue *cq,
		    dma_addr_t dst, dma_addr_t *src, u32 src_cnt, u32 size, u64 flags)
{
	u32 s[5];
	int i;

	if (hse_should_workaround_copy(hse_dev) || src_cnt <= 1 || src_cnt > 5)
		return -EINVAL;

	for (i = 0; i < src_cnt; i++)
		s[i] = src[i];

	return __append_xor(cq, dst, s, src_cnt, size);
}

int hse_cq_prep_xor_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
		       struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
		       struct scatterlist **src_sg, u32 *src_nents, u32 *src_offset,
		       u32 src_cnt, u32 size, u64 flags)
{
	u32 dst_addr, src_addr[5];
	u32 i = 0, j[5] = { 0 }, k;
	u32 dst_len, src_len[5];
	u32 chunk_size;
	int ret;

	if (hse_should_workaround_copy(hse_dev) || src_cnt <= 1 || src_cnt > 5)
		return -EINVAL;

	while (i < dst_nents && size > 0) {
		for (k = 0; k < src_cnt; k++)
			if (j[k] >= src_nents[k])
				break;
		if (k != src_cnt)
			break;

		dst_len = sg_dma_len(dst_sg);
		if (dst_offset >= dst_len) {
			dst_offset -= dst_len;
			dst_sg = sg_next(dst_sg);
			i += 1;
			continue;
		}
		dst_addr = sg_dma_address(dst_sg) + dst_offset;
		chunk_size = min(size, dst_len - dst_offset);

		for (k = 0; k < src_cnt; k++) {
			src_len[k] = sg_dma_len(src_sg[k]);
			if (src_offset[k] >= src_len[k]) {
				src_offset[k] -= src_len[k];
				src_sg[k] = sg_next(src_sg[k]);
				j[k] += 1;
				break;
			}

			src_addr[k] = sg_dma_address(src_sg[k]) + src_offset[k];
			chunk_size = min(chunk_size, src_len[k] - src_offset[k]);
		}
		if (k != src_cnt)
			continue;

		ret = __append_xor(cq, dst_addr, src_addr, src_cnt, chunk_size);
		if (ret)
			return ret;

		dst_offset += chunk_size;
		for (k = 0; k < src_cnt; k++)
			src_offset[k] += chunk_size;
		size -= chunk_size;
	}

	return size != 0 ? -EINVAL : 0;
}

#define YUY2_TO_NV16_SRC_BYTES_PER_PIXEL 2

static bool yuy2_to_nv16_args_valid(u32 luma, u32 chroma, u16 dst_pitch, u32 src, u16 src_pitch, u16 width,
				u16 height)
{
	return IS_ALIGNED(luma, 2) && IS_ALIGNED(chroma, 2) &&  IS_ALIGNED(dst_pitch, 2) &&
		(dst_pitch >= width) &&	IS_ALIGNED(src, 4) && IS_ALIGNED(src_pitch, 4) &&
		(src_pitch >= width * YUY2_TO_NV16_SRC_BYTES_PER_PIXEL) && IS_ALIGNED(width, 2);
}

int hse_cq_prep_yuy2_to_nv16_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				       struct scatterlist *luma_sg, u32 luma_nents, u32 luma_offset,
				       struct scatterlist *chroma_sg, u32 chroma_nents, u32 chroma_offset,
				       u16 dst_pitch, struct scatterlist *src_sg, u32 src_nents, u32 src_offset,
				       u16 src_pitch, u16 width, u16 height, u64 flags)
{
	u32 i = 0, j = 0, k = 0;
	u32 luma_len, chroma_len, src_len;
	u32 luma_addr, chroma_addr, src_addr;
	u32 npixel, left = width;
	int ret;

	if (!yuy2_to_nv16_args_valid(luma_offset, chroma_offset, dst_pitch, src_offset,
					    src_pitch, width, height))
		return -EINVAL;

	while (i < luma_nents && j < chroma_nents && k < src_nents && height > 0) {
		luma_len = sg_dma_len(luma_sg);
		if (luma_offset >= luma_len) {
			luma_offset -= luma_len;
			luma_sg = sg_next(luma_sg);
			i += 1;
			continue;
		}

		chroma_len = sg_dma_len(chroma_sg);
		if (chroma_offset >= chroma_len) {
			chroma_offset -= chroma_len;
			chroma_sg = sg_next(chroma_sg);
			j += 1;
			continue;
		}

		src_len = sg_dma_len(src_sg);
		if (src_offset >= src_len) {
			src_offset -= src_len;
			src_sg = sg_next(src_sg);
			k += 1;
			continue;
		}

		luma_addr = sg_dma_address(luma_sg) + luma_offset;
		chroma_addr = sg_dma_address(chroma_sg) + chroma_offset;
		src_addr = sg_dma_address(src_sg) + src_offset;

		if (left == width) {
			u32 h = min3((luma_len - luma_offset) / dst_pitch,
				(chroma_len - chroma_offset) / dst_pitch,
				(src_len - src_offset) / src_pitch);

			h = min((u32)height, h);
			if (h) {
				ret = __append_yuy2_to_nv16(cq, luma_addr, chroma_addr,
								   dst_pitch, src_addr, src_pitch,
								   width, h, flags);
				if (ret)
					return ret;

				luma_offset   += dst_pitch * h;
				chroma_offset += dst_pitch * h;
				src_offset    += src_pitch * h;
				height        -= h;
				continue;
			}

		}

		npixel = min3((luma_len - luma_offset), (chroma_len - chroma_offset),
			(src_len - src_offset) / YUY2_TO_NV16_SRC_BYTES_PER_PIXEL);
		npixel = min(npixel, left);

		ret = __append_yuy2_to_nv16(cq, luma_addr, chroma_addr, npixel, src_addr,
					      npixel * YUY2_TO_NV16_SRC_BYTES_PER_PIXEL,
					      npixel, 1, flags);
		if (ret)
			return ret;

		luma_offset   += npixel;
		chroma_offset += npixel;
		src_offset    += npixel * YUY2_TO_NV16_SRC_BYTES_PER_PIXEL;
		left -= npixel;

		if (left == 0) {
			left = width;
			luma_offset   += dst_pitch - width;
			chroma_offset += dst_pitch - width;
			src_offset    += src_pitch - width * YUY2_TO_NV16_SRC_BYTES_PER_PIXEL;
			height        -= 1;
		}
	}

	return height != 0 ? -EINVAL : 0;
}

static int __append_rotate(struct hse_command_queue *cq,
			   u32 dst, u32 dst_pitch, u32 src, u32 src_pitch,
			   u32 width, u32 height, u32 mode, u32 color_format)
{
	u32 cmds[5];

	cmds[0] = 0x5 | (mode << 29) | (color_format << 31);
	cmds[1] = height | (width << 16);
	cmds[2] = dst_pitch | (src_pitch << 16);
	cmds[3] = dst;
	cmds[4] = src;
	return hse_cq_add_data(cq, cmds, 5);
}

int hse_cq_prep_rotate(struct hse_device *hse_dev, struct hse_command_queue *cq,
		       dma_addr_t dst, u32 dst_pitch, dma_addr_t src, u32 src_pitch,
		       u32 width, u32 height, u32 mode, u32 color_format)
{
	return __append_rotate(cq, dst, dst_pitch, src, src_pitch, width, height, mode, color_format);
}
