// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Copyright (c) 2020-2024 Realtek Semiconductor Corp.
 */

#include <asm/cacheflush.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sha1_base.h>
#include <crypto/sha256_base.h>
#include <crypto/sha512_base.h>
#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>

#include "rtk-mcp.h"

/* Static structures */
static u32 aes_256_key[AES_KEYSIZE_256/4];
struct platform_device *mcp_pdev;
static void __iomem *mcp_iobase;
static spinlock_t lock;
static int chip_id;

static void setup_chip_id(void)
{
	const struct soc_device_attribute *match = NULL;
	struct soc_device_attribute attrs[] = {
		{ .family = "Realtek Hercules", .data = (void *)CHIP_ID_RTD1395, },
		{ .family = "Realtek Thor",     .data = (void *)CHIP_ID_RTD1619, },
		{ .family = "Realtek Hank",     .data = (void *)CHIP_ID_RTD1319, },
		{ .family = "Realtek Stark",    .data = (void *)CHIP_ID_RTD1619B, },
		{}
	};

	match = soc_device_match(attrs);
	if (match)
		chip_id = (unsigned long)match->data;
}

static unsigned int rtk_mcp_crypt(struct rtk_mcp_op *op)
{
	struct rtk_mcp_op *p_desc_vptr;
	int counter2 = 0x3ff << 2;
	dma_addr_t p_desc_ptr;
	unsigned long iflags;
	int counter1 = 30;
	u32 status;
	int ret;

	if (op->len == 0)
		return 0;

	/* Start the critical section */
	spin_lock_irqsave(&lock, iflags);

	p_desc_vptr = dma_alloc_coherent(&mcp_pdev->dev,
					 sizeof(struct rtk_mcp_op),
					 &p_desc_ptr,
					 GFP_ATOMIC|GFP_DMA);

	if (!p_desc_vptr) {
		pr_err("%s dmalloc error !\n", __func__);
		spin_unlock_irqrestore(&lock, iflags);
		return 0;
	}

	p_desc_vptr->flags = op->flags;
	memcpy(p_desc_vptr->key, op->key, sizeof(p_desc_vptr->key));
	memcpy(p_desc_vptr->iv, op->iv, sizeof(p_desc_vptr->iv));
	p_desc_vptr->src = op->src;
	p_desc_vptr->dst = op->dst;
	p_desc_vptr->len = op->len;

	iowrite32(p_desc_ptr, mcp_iobase + MCP_BASE);
	iowrite32(p_desc_ptr + sizeof(struct rtk_mcp_op) + sizeof(struct rtk_mcp_op), mcp_iobase + MCP_LIMIT);
	iowrite32(p_desc_ptr, mcp_iobase + MCP_RDPTR);
	iowrite32(p_desc_ptr + sizeof(struct rtk_mcp_op), mcp_iobase + MCP_WRPTR);

	iowrite32(0x0, mcp_iobase + MCP_DES_COUNT);
	iowrite32(MCP_CLEAR | MCP_WRITE_DATA_1, mcp_iobase + MCP_CTRL);

	do {
		status = ioread32(mcp_iobase + MCP_CTRL);
		cpu_relax();
	} while ((status & MCP_CLEAR) && --counter1);

	if (status & MCP_CLEAR) {
		/* write 0 to unset clear bit*/
		iowrite32(MCP_CLEAR, mcp_iobase + MCP_CTRL);
	}

	iowrite32(0xfe, mcp_iobase + MCP_EN);
	iowrite32(0xfe, mcp_iobase + MCP_STATUS);

	iowrite32(MCP_GO | MCP_WRITE_DATA_1, mcp_iobase + MCP_CTRL);

	do {
		status = ioread32(mcp_iobase + MCP_CTRL);
		if (!(status & MCP_GO)) {
			pr_err("ctrl break\n");
			break;
		}
		status = ioread32(mcp_iobase + MCP_STATUS);
		if (status & 0x6) {
			pr_err("status break\n");
			break;
		}
		/* will set as msleep afte test from probe*/
		mdelay(1);
	} while (--counter2);

	if (chip_id == CHIP_ID_RTD1295 || chip_id == CHIP_ID_RTD1395)
		ret = (ioread32(mcp_iobase + MCP_STATUS) & ~(MCP_RING_EMPTY | MCP_COMPARE)) ? -1 : 0;
	else
		ret = (ioread32(mcp_iobase + MCP_STATUS) & ~(MCP_RING_EMPTY | MCP_COMPARE | MCP_KL_DONE | MCP_K_KL_DONE)) ? -1 : 0;

	if (ret < 0) {
		pr_err("do mcp command failed, (MCP_Status %08x)\n", ioread32(mcp_iobase + MCP_STATUS));

		ret = 0;
		goto error;
	}

	iowrite32(MCP_GO, mcp_iobase + MCP_CTRL);
	iowrite32(0xfe, mcp_iobase + MCP_STATUS);

	ret = op->len;
error:

	dma_free_coherent(&mcp_pdev->dev, sizeof(struct rtk_mcp_op), (void *)p_desc_vptr, p_desc_ptr);
	spin_unlock_irqrestore(&lock, iflags);

	return ret;
}

static int rtk_setkey_blk(struct crypto_skcipher *tfm, const u8 *key, unsigned int len)
{
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	int i;

	if (key == NULL) {
		op->flags = MCP_KEY_SEL(MCP_KEY_SEL_OTP);
		return 0;
	}

	if (len == AES_KEYSIZE_256) {
		/* special handling */
		for (i = 0; i < len/4; i++)
			aes_256_key[i] = *((const u32 *)key + i);

		for (i = 0; i < sizeof(op->key)/4; i++)
			op->key[i] = 0;

		op->flags = MCP_ALGO_AES_256 | MCP_KEY_SEL(MCP_KEY_SEL_DDR);
	} else {
		for (i = 0; i < len/4; i++)
			op->key[i] = cpu_to_be32(*((const u32 *)key + i));

		if (len == AES_KEYSIZE_128)
			op->flags = MCP_ALGO_AES;

		if (len == AES_KEYSIZE_192)
			op->flags = MCP_ALGO_AES_192;
	}

	return 0;
}

static void rtk_sha1_transform(struct sha1_state *sctx, u8 *src, int blocks)
{
	unsigned char *dma_vaddr;
	struct rtk_mcp_op *op;
	dma_addr_t dma_paddr;
	int i, ret;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return;

	op->flags = MCP_ALGO_SHA_1;
	op->src = dma_map_single(&mcp_pdev->dev, src, blocks * SHA1_BLOCK_SIZE, DMA_TO_DEVICE);
	dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, SHA1_DIGEST_SIZE, &dma_paddr, GFP_ATOMIC|GFP_DMA);
	op->dst = dma_paddr;

	for (i = 0; i < SHA1_DIGEST_SIZE/4; i++)
		op->key[i] = (*((const u32 *)sctx->state + i));

	op->len = blocks * SHA1_BLOCK_SIZE;

	/* disable auto padding */
	iowrite32(0x800, mcp_iobase + MCP_CTRL1);
	ret = rtk_mcp_crypt(op);

	for (i = 0; i < SHA1_DIGEST_SIZE/4; i++)
		sctx->state[i] = cpu_to_be32(*((const u32 *)dma_vaddr + i));

	dma_free_coherent(&mcp_pdev->dev, SHA1_DIGEST_SIZE, (void *)dma_vaddr, dma_paddr);
	dma_unmap_single(&mcp_pdev->dev, op->src, blocks * SHA1_BLOCK_SIZE, DMA_TO_DEVICE);

	kfree(op);
}

static int rtk_sha1_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
	return sha1_base_do_update(desc, data, len, (sha1_block_fn *)rtk_sha1_transform);
}

static int rtk_sha1_final(struct shash_desc *desc, u8 *out)
{
	sha1_base_do_finalize(desc, (sha1_block_fn *)rtk_sha1_transform);
	return sha1_base_finish(desc, out);
}

static int rtk_sha1_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *hash)
{
	sha1_base_do_update(desc, data, len, (sha1_block_fn *)rtk_sha1_transform);
	return rtk_sha1_final(desc, hash);
}

static struct shash_alg rtk_sha1_alg = {
	.digestsize	= SHA1_DIGEST_SIZE,
	.init		= sha1_base_init,
	.update		= rtk_sha1_update,
	.final		= rtk_sha1_final,
	.finup		= rtk_sha1_finup,
	.descsize	= sizeof(struct sha1_state),
	.base = {
		.cra_name		= "sha1",
		.cra_driver_name	= "__driver_sha1-rtk",
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static void rtk_sha256_transform(struct sha256_state *sctx, u8 *src, int blocks)
{
	unsigned char *dma_vaddr;
	struct rtk_mcp_op *op;
	dma_addr_t dma_paddr;
	int i, ret;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return;

	op->flags = MCP_ALGO_SHA_256;
	op->src = dma_map_single(&mcp_pdev->dev, src, blocks * SHA256_BLOCK_SIZE, DMA_TO_DEVICE);
	dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, SHA256_DIGEST_SIZE, &dma_paddr, GFP_ATOMIC|GFP_DMA);
	op->dst = dma_paddr;

	for (i = 0; i < sizeof(op->key)/4; i++)
		op->key[i] = (*((const u32 *)sctx->state + i));

	op->iv[0] = (*((const u32 *)sctx->state + sizeof(op->key)/4));
	op->iv[1] = (*((const u32 *)sctx->state + sizeof(op->key)/4 + 1));

	op->len = blocks * SHA256_BLOCK_SIZE;

	/* disable auto padding */
	iowrite32(0x800, mcp_iobase + MCP_CTRL1);
	ret = rtk_mcp_crypt(op);

	for (i = 0; i < SHA256_DIGEST_SIZE/4; i++)
		sctx->state[i] = cpu_to_be32(*((const u32 *)dma_vaddr + i));

	dma_free_coherent(&mcp_pdev->dev, SHA256_DIGEST_SIZE, (void *)dma_vaddr, dma_paddr);
	dma_unmap_single(&mcp_pdev->dev, op->src, blocks * SHA256_BLOCK_SIZE, DMA_TO_DEVICE);

	kfree(op);
}

static int rtk_sha256_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	return sha256_base_do_update(desc, data, len, (sha256_block_fn *)rtk_sha256_transform);
}

static int rtk_sha256_final(struct shash_desc *desc, u8 *out)
{
	sha256_base_do_finalize(desc, (sha256_block_fn *)rtk_sha256_transform);
	return sha256_base_finish(desc, out);
}

static int rtk_sha256_finup(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *hash)
{
	sha256_base_do_update(desc, data, len, (sha256_block_fn *)rtk_sha256_transform);
	return rtk_sha256_final(desc, hash);
}

static struct shash_alg rtk_sha256_alg = {
	.digestsize	= SHA256_DIGEST_SIZE,
	.init		= sha256_base_init,
	.update		= rtk_sha256_update,
	.final		= rtk_sha256_final,
	.finup		= rtk_sha256_finup,
	.descsize	= sizeof(struct sha256_state),
	.base = {
		.cra_name		= "sha256",
		.cra_driver_name	= "__driver_sha256-rtk",
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= SHA256_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static void rtk_sha512_transform(struct sha512_state *sctx, u8 *src, int blocks)
{
	unsigned char *dma_vaddr;
	struct rtk_mcp_op *op;
	dma_addr_t dma_paddr;
	int i, ret;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return;

	op->flags = MCP_ALGO_SHA_512 | MCP_KEY_SEL(MCP_KEY_SEL_DDR);
	op->src = dma_map_single(&mcp_pdev->dev, src, blocks * SHA512_BLOCK_SIZE, DMA_TO_DEVICE);
	dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, SHA512_DIGEST_SIZE, &dma_paddr, GFP_ATOMIC|GFP_DMA);
	op->dst = dma_paddr;

	for (i = 0; i < SHA512_DIGEST_SIZE/8; i++)
		sctx->state[i] = cpu_to_be64(sctx->state[i]);

	op->key[0] = dma_map_single(&mcp_pdev->dev, sctx->state, SHA512_DIGEST_SIZE, DMA_TO_DEVICE);
	op->len = blocks * SHA512_BLOCK_SIZE;

	/* disable auto padding */
	iowrite32(0x800, mcp_iobase + MCP_CTRL1);
	ret = rtk_mcp_crypt(op);

	dma_unmap_single(&mcp_pdev->dev, op->key[0], SHA512_DIGEST_SIZE, DMA_TO_DEVICE);
	dma_unmap_single(&mcp_pdev->dev, op->src, blocks * SHA512_BLOCK_SIZE, DMA_TO_DEVICE);

	for (i = 0; i < SHA512_DIGEST_SIZE/8; i++)
		sctx->state[i] = cpu_to_be64(*((const u64 *)dma_vaddr + i));

	dma_free_coherent(&mcp_pdev->dev, SHA512_DIGEST_SIZE, (void *)dma_vaddr, dma_paddr);

	kfree(op);
}

static int rtk_sha512_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
	return sha512_base_do_update(desc, data, len, (sha512_block_fn *)rtk_sha512_transform);
}

static int rtk_sha512_final(struct shash_desc *desc, u8 *out)
{
	sha512_base_do_finalize(desc, (sha512_block_fn *)rtk_sha512_transform);
	return sha512_base_finish(desc, out);
}

static int rtk_sha512_finup(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *hash)
{
	sha512_base_do_update(desc, data, len, (sha512_block_fn *)rtk_sha512_transform);
	return rtk_sha512_final(desc, hash);
}

static struct shash_alg rtk_sha512_alg = {
	.digestsize	= SHA512_DIGEST_SIZE,
	.init		= sha512_base_init,
	.update		= rtk_sha512_update,
	.final		= rtk_sha512_final,
	.finup		= rtk_sha512_finup,
	.descsize	= sizeof(struct sha512_state),
	.base = {
		.cra_name		= "sha512",
		.cra_driver_name	= "__driver_sha512-rtk",
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= SHA512_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int rtk_des_setkey_blk(struct crypto_skcipher *tfm, const u8 *key, unsigned int len)
{
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	int i;

	if (key == NULL) {
		op->flags = MCP_KEY_SEL(MCP_KEY_SEL_OTP);
		return 0;
	}

	op->flags = 0;

	for (i = 0; i < len/4; i++)
		op->key[i] = cpu_to_be32(*((const u32 *)key + i));

	return 0;
}

static int rtk_des_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % DES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;
		op->flags = DES_ECB_DEC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static int rtk_des_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % DES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;
		op->flags = DES_ECB_ENC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static struct skcipher_alg rtk_des_ecb_alg = {
	.base.cra_name = "ecb(des)",
	.base.cra_driver_name = "__drive-ecb_des-rtk",
	.base.cra_priority = 400,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = DES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct rtk_mcp_op),
	.base.cra_alignmask = 15,
	.base.cra_module = THIS_MODULE,

	.setkey = rtk_des_setkey_blk,
	.decrypt = rtk_des_ecb_decrypt,
	.encrypt = rtk_des_ecb_encrypt,
	.min_keysize = DES_KEY_SIZE,
	.max_keysize = DES_KEY_SIZE,
	.ivsize = DES_BLOCK_SIZE,
};

static int rtk_ctr_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_CTR_DEC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static int rtk_ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_CTR_ENC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static struct skcipher_alg rtk_aes_ctr_alg = {
	.base.cra_name = "ctr(aes)",
	.base.cra_driver_name = "__driver_ctr-aes-rtk",
	.base.cra_priority = 400,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct rtk_mcp_op),
	.base.cra_alignmask = 15,
	.base.cra_module = THIS_MODULE,

	.setkey = rtk_setkey_blk,
	.decrypt = rtk_ctr_decrypt,
	.encrypt = rtk_ctr_encrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
};

static int rtk_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_CBC_DEC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static int rtk_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int i, err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	for (i = 0; i < sizeof(op->iv)/4; i++)
		op->iv[i] = cpu_to_be32(*((const u32 *)walk.iv + i));

	while ((nbytes = walk.nbytes) != 0) {

		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_CBC_ENC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static struct skcipher_alg rtk_aes_cbc_alg = {
	.base.cra_name = "cbc(aes)",
	.base.cra_driver_name = "__driver_cbc-aes-rtk",
	.base.cra_priority = 400,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct rtk_mcp_op),
	.base.cra_alignmask = 15,
	.base.cra_module = THIS_MODULE,

	.setkey = rtk_setkey_blk,
	.decrypt = rtk_cbc_decrypt,
	.encrypt = rtk_cbc_encrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
};

static int rtk_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	memcpy(op->iv, walk.iv, sizeof(op->iv));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_ECB_DEC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static int rtk_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rtk_mcp_op *op = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned char *dma_vaddr;
	dma_addr_t dma_paddr;
	unsigned int nbytes;
	int err, ret;

	err = skcipher_walk_virt(&walk, req, false);

	memcpy(op->iv, walk.iv, sizeof(op->iv));

	while ((nbytes = walk.nbytes) != 0) {
		op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
		op->src = dma_map_single(&mcp_pdev->dev, walk.src.virt.addr, op->len, DMA_TO_DEVICE);
		dma_vaddr = dma_alloc_coherent(&mcp_pdev->dev, op->len, &dma_paddr, GFP_ATOMIC|GFP_DMA);
		op->dst = dma_paddr;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			op->key[0] = dma_map_single(&mcp_pdev->dev, aes_256_key, AES_KEYSIZE_256, DMA_TO_DEVICE);

		op->flags |= AES_ECB_ENC;

		ret = rtk_mcp_crypt(op);
		nbytes -= ret;

		if (MCP_MODE(op->flags) == MCP_ALGO_AES_256)
			dma_unmap_single(&mcp_pdev->dev, op->key[0], AES_KEYSIZE_256, DMA_TO_DEVICE);

		err = skcipher_walk_done(&walk, nbytes);

		memcpy(walk.dst.virt.addr, dma_vaddr, op->len);
		dma_free_coherent(&mcp_pdev->dev, op->len, (void *)dma_vaddr, dma_paddr);
		dma_unmap_single(&mcp_pdev->dev, op->src, op->len, DMA_TO_DEVICE);
	}

	return err;
}

static struct skcipher_alg rtk_aes_ecb_alg = {
	.base.cra_name = "ecb(aes)",
	.base.cra_driver_name = "__driver_ecb-aes-rtk",
	.base.cra_priority = 400,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct rtk_mcp_op),
	.base.cra_alignmask = 15,
	.base.cra_module = THIS_MODULE,

	.setkey = rtk_setkey_blk,
	.decrypt = rtk_ecb_decrypt,
	.encrypt = rtk_ecb_encrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
};

#if 0
static void rtk_mcp_aes128_ecb_encrypt_test(struct platform_device *pdev)
{
	unsigned char Data[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
				  0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
				  0xee, 0xff};

	unsigned char key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
				 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
				 0x0e, 0x0f};
	unsigned char *dma_vaddr;
	struct rtk_mcp_op *op;
	dma_addr_t dma_paddr;
	int i, ret;

	op = kzalloc(sizeof(struct rtk_mcp_op), GFP_KERNEL);

	for (i = 0; i < sizeof(key)/4; i++)
		op->key[i] = cpu_to_be32(*((const u32 *)key + i));

	dma_vaddr = dma_alloc_coherent(&pdev->dev, sizeof(Data), &dma_paddr, GFP_ATOMIC|GFP_DMA);
	memcpy(dma_vaddr, Data, sizeof(Data));

	op->src = dma_paddr;
	op->dst = dma_paddr;
	op->flags = AES_ECB_ENC;
	op->len = sizeof(Data);

	ret = rtk_mcp_crypt(op);
	if (ret == 0) {
		pr_err("encrypt engine fail\n");
		return;
	}

	op->src = dma_paddr;
	op->dst = dma_paddr;
	op->flags = AES_ECB_DEC;
	op->len = sizeof(Data);

	ret = rtk_mcp_crypt(op);
	if (ret == 0) {
		pr_err("decrypt engine fail\n");
		return;
	}

	dma_free_coherent(&mcp_pdev->dev, sizeof(Data), (void *)dma_vaddr, dma_paddr);
}
#endif

static int rtk_mcp_phy_init(void)
{
	/* dessert go bit */
	iowrite32(MCP_GO, mcp_iobase + MCP_CTRL);

	/* disable all interrupts */
	iowrite32(0xfe, mcp_iobase + MCP_EN);

	/* clear interrupts status */
	iowrite32(0xfe, mcp_iobase + MCP_STATUS);
	iowrite32(0, mcp_iobase + MCP_BASE);
	iowrite32(0, mcp_iobase + MCP_LIMIT);
	iowrite32(0, mcp_iobase + MCP_RDPTR);
	iowrite32(0, mcp_iobase + MCP_WRPTR);

	if (chip_id == CHIP_ID_RTD1395) {
		unsigned int cur_value;

		/* auto power management */
		cur_value = ioread32(mcp_iobase + PWM_CTRL);
		cur_value |= (1 << 22) | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 27) | (1 << 28);
		iowrite32(cur_value, mcp_iobase + PWM_CTRL);
	}

	return 0;
}

static int rtk_mcp_probe(struct platform_device *pdev)
{
	int ret;

	setup_chip_id();

	spin_lock_init(&lock);
	mcp_pdev = pdev;

	mcp_iobase = of_iomap(pdev->dev.of_node, 0);
	if (!mcp_iobase) {
		dev_err(&pdev->dev, "no mcp address\n");
		return -EINVAL;
	}

	rtk_mcp_phy_init();

	ret = crypto_register_skciphers(&rtk_aes_ecb_alg, sizeof(rtk_aes_ecb_alg));
	if (ret)
		goto err;
	ret = crypto_register_skciphers(&rtk_aes_cbc_alg, sizeof(rtk_aes_cbc_alg));
	if (ret)
		goto err;
	ret = crypto_register_skciphers(&rtk_aes_ctr_alg, sizeof(rtk_aes_ctr_alg));
	if (ret)
		goto err;
	ret = crypto_register_shash(&rtk_sha256_alg);
	if (ret)
		goto err;
	ret = crypto_register_shash(&rtk_sha512_alg);
	if (ret)
		goto err;
	ret = crypto_register_shash(&rtk_sha1_alg);
	if (ret)
		goto err;
	ret = crypto_register_skciphers(&rtk_des_ecb_alg, sizeof(rtk_des_ecb_alg));
	if (ret)
		goto err;

	platform_set_drvdata(pdev, mcp_pdev);

	dev_notice(&pdev->dev, "MCP engine enabled\n");
	return 0;
err:
	dev_err(&pdev->dev, "MCP initialization failed\n");
	return ret;
}

static int rtk_mcp_remove(struct platform_device *dev)
{
	crypto_unregister_skciphers(&rtk_aes_ecb_alg, sizeof(rtk_aes_ecb_alg));
	crypto_unregister_skciphers(&rtk_aes_cbc_alg, sizeof(rtk_aes_cbc_alg));
	crypto_unregister_skciphers(&rtk_aes_ctr_alg, sizeof(rtk_aes_ctr_alg));
	crypto_unregister_skciphers(&rtk_des_ecb_alg, sizeof(rtk_des_ecb_alg));

	return 0;
}

static struct of_device_id rtk_mcp_ids[] = {
	{ .compatible = "realtek,rtk-mcp" },
	{ /* Sentinel */ },
};

static struct platform_driver rtk_mcp_driver = {
	.probe		= rtk_mcp_probe,
	.remove		= rtk_mcp_remove,
	.driver		= {
		.name		= "RTK_MCP",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(rtk_mcp_ids),
	},
};

module_platform_driver(rtk_mcp_driver);

MODULE_AUTHOR("Realtek");
MODULE_DESCRIPTION("Realtek MCP driver");
MODULE_LICENSE("GPL");
