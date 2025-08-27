// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mpage.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/of_address.h>
#include <linux/syscalls.h>
#include <linux/dma-map-ops.h>
#include <sound/asound.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#include <soc/realtek/rtk_refclk.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include "snd-hifi-realtek.h"
#include <asm/cacheflush.h>

#define PCM_SUBSTREAM_CHECK(sub) snd_BUG_ON(!(sub))

/* Playback options */
static int snd_card_playback_open(struct snd_pcm_substream *substream);
static int snd_card_playback_close(struct snd_pcm_substream *substream);
static int snd_card_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params);
static int snd_card_hw_free(struct snd_pcm_substream *substream);
static int snd_card_playback_prepare(struct snd_pcm_substream *substream);
static int snd_card_playback_trigger(struct snd_pcm_substream *substream, int cmd);
static int snd_card_playback_mmap(struct snd_pcm_substream *substream,
				  struct vm_area_struct *area);
static int snd_card_playback_ack(struct snd_pcm_substream *substream);
static snd_pcm_uframes_t snd_card_playback_pointer(struct snd_pcm_substream *substream);

/* Capture options */
static int snd_card_capture_open(struct snd_pcm_substream *substream);
static int snd_card_capture_close(struct snd_pcm_substream *substream);
static int snd_card_capture_prepare(struct snd_pcm_substream *substream);
static int snd_card_capture_trigger(struct snd_pcm_substream *substream, int cmd);
static int snd_card_capture_mmap(struct snd_pcm_substream *substream,
				 struct vm_area_struct *area);
static snd_pcm_uframes_t snd_card_capture_pointer(struct snd_pcm_substream *substream);

static unsigned int snd_capture_monitor_delay(struct snd_pcm_substream *substream);
static int snd_card_capture_get_time_info(struct snd_pcm_substream *substream,
					  struct timespec64 *system_ts, struct timespec64 *audio_ts,
					  struct snd_pcm_audio_tstamp_config *audio_tstamp_config,
			struct snd_pcm_audio_tstamp_report *audio_tstamp_report);
static int rtk_snd_capture_malloc_pts_ring(struct snd_pcm_runtime *runtime);
static int rtk_snd_capture_PTS_ringheader_AI(struct snd_pcm_runtime *runtime);
static void snd_card_capture_setup_pts(struct snd_pcm_runtime *runtime,
				       struct audio_dec_pts_info *pkt);
static int rtk_snd_capture_hdmirx_enable(void);
static void rtk_snd_capture_handle_HDMI_plug_out(struct snd_pcm_substream *substream);

static void rtk_playback_realtime_function(struct snd_pcm_runtime *runtime);
static enum hrtimer_restart rtk_capture_timer_function(struct hrtimer *timer);
static void snd_card_capture_calculate_pts(struct snd_pcm_runtime *runtime,
					   long period_count);

static unsigned long valid_free_size(unsigned long base,
				     unsigned long limit,
				     unsigned long rp,
				     unsigned long wp);

static unsigned long ring_add(unsigned long ring_base,
			      unsigned long ring_limit,
			      unsigned long ptr,
			      unsigned int bytes);

static unsigned long ring_valid_data(unsigned long ring_base,
				     unsigned long ring_limit,
				     unsigned long ring_rp,
				     unsigned long ring_wp);

static unsigned long buf_memcpy2_ring(unsigned long base,
				      unsigned long limit,
				      unsigned long ptr,
				      char *buf,
				      unsigned long size);

static int ring_check_ptr_valid_32(unsigned int ring_rp,
				   unsigned int ring_wp,
				   unsigned int ptr);

static long ring_memcpy2_buf(char *buf,
			     unsigned long base,
			     unsigned long limit,
			     unsigned long ptr,
			     unsigned int size);

static unsigned long ring_minus(unsigned long ring_base,
				unsigned long ring_limit,
				unsigned long ptr,
				int bytes);

static int snd_realtek_hw_free_ring(struct snd_pcm_runtime *runtime);
static int snd_RTK_volume_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo);
static int snd_RTK_volume_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
static int snd_RTK_volume_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
static int snd_RTK_capsrc_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo);
static int snd_RTK_capsrc_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
static int snd_RTK_capsrc_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);

static u64 rtk_get_90k_pts_hifi(void);
static int snd_realtek_hw_capture_free_ring(struct snd_pcm_runtime *runtime);
static long snd_card_get_ring_data(struct ringbuffer_header *ring);
static void ring1_to_ring2_general_64(struct audio_ringbuf_ptr_64 *ring1,
				      struct audio_ringbuf_ptr_64 *ring2,
				      long size);

/* GLOBAL */
static char *snd_pcm_id[] = {"Skip decoder normal/HDMI-RX",
			     "Skip decoder mmap/I2S",
			     "Skip decoder deepbuffer/non pcm",
			     "With decoder/audio_aec_mix",
			     "rtk_snd_audio_v2_in",
			     "rtk_snd_audio_v3_in",
			     "rtk_snd_audio_v4_in",
			     "rtk_snd_i2s_loopback_in",
			     "rtk_snd_dmic_passthrough_in",
			     "rtk_snd_pure_dmic_in"};

static void __iomem *sys_clk_en2_virt;
static int snd_open_count;
static int snd_open_ai_count;
static int mtotal_latency;
static unsigned int rtk_dec_ao_buffer;
static bool is_suspend;

static struct snd_kcontrol_new rtk_snd_controls[] = {
	RTK_VOLUME("Master Volume", 0, MIXER_ADDR_MASTER),
	RTK_CAPSRC("Master Capture Switch", 0, MIXER_ADDR_MASTER),
	RTK_VOLUME("Synth Volume", 0, MIXER_ADDR_SYNTH),
};

struct rtk_krpc_ept_info *hifi_ept_info;
struct rtk_alsa_device {
	struct device *dev;
	struct snd_card *card;
};

static struct snd_pcm_ops snd_card_rtk_playback_ops = {
	.open =         snd_card_playback_open,
	.close =        snd_card_playback_close,
	.hw_params =    snd_card_hw_params,
	.hw_free =      snd_card_hw_free,
	.prepare =      snd_card_playback_prepare,
	.trigger =      snd_card_playback_trigger,
	.pointer =      snd_card_playback_pointer,
	.mmap =         snd_card_playback_mmap,
	.ack =          snd_card_playback_ack
};

static struct snd_pcm_ops snd_card_rtk_capture_ops = {
	.open =         snd_card_capture_open,
	.close =        snd_card_capture_close,
	.hw_params =    snd_card_hw_params,
	.hw_free =      snd_card_hw_free,
	.prepare =      snd_card_capture_prepare,
	.trigger =      snd_card_capture_trigger,
	.pointer =      snd_card_capture_pointer,
	.mmap =         snd_card_capture_mmap,
	.get_time_info = snd_card_capture_get_time_info
};

static struct snd_pcm_hardware snd_card_playback = {
	.info               = RTK_DMP_PLAYBACK_INFO,
	.formats            = RTK_DMP_PLAYBACK_FORMATS,
	.rates              = RTK_DMP_PLYABACK_RATES,
	.rate_min           = RTK_DMP_PLAYBACK_RATE_MIN,
	.rate_max           = RTK_DMP_PLAYBACK_RATE_MAX,
	.channels_min       = RTK_DMP_PLAYBACK_CHANNELS_MIN,
	.channels_max       = RTK_DMP_PLAYBACK_CHANNELS_MAX,
	.buffer_bytes_max   = RTK_DMP_PLAYBACK_MAX_BUFFER_SIZE,
	.period_bytes_min   = RTK_DMP_PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max   = RTK_DMP_PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min        = RTK_DMP_PLAYBACK_PERIODS_MIN,
	.periods_max        = RTK_DMP_PLAYBACK_PERIODS_MAX,
	.fifo_size          = RTK_DMP_PLAYBACK_FIFO_SIZE,
};

static struct snd_pcm_hardware rtk_snd_card_capture = {
	.info               = RTK_DMP_CAPTURE_INFO,
	.formats            = RTK_DMP_CAPTURE_FORMATS,
	.rates              = RTK_DMP_CAPTURE_RATES,
	.rate_min           = RTK_DMP_CAPTURE_RATE_MIN,
	.rate_max           = RTK_DMP_CAPTURE_RATE_MAX,
	.channels_min       = RTK_DMP_CAPTURE_CHANNELS_MIN,
	.channels_max       = RTK_DMP_CAPTURE_CHANNELS_MAX,
	.buffer_bytes_max   = RTK_DMP_CAPTURE_MAX_BUFFER_SIZE,
	.period_bytes_min   = RTK_DMP_CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max   = RTK_DMP_CAPTURE_MAX_PERIOD_SIZE,
	.periods_min        = RTK_DMP_CAPTURE_PERIODS_MIN,
	.periods_max        = RTK_DMP_CAPTURE_PERIODS_MAX,
	.fifo_size          = RTK_DMP_CAPTURE_FIFO_SIZE,
};

static int rtk_snd_alloc(struct device *dev, size_t size,
			 void *ion_phy, void **ion_virt,
			 unsigned long heap_flags)
{
	mutex_lock(&dev->mutex);
	rheap_setup_dma_pools(dev, "rtk_media_heap", heap_flags, __func__);
	*ion_virt = dma_alloc_coherent(dev, size, ion_phy, GFP_KERNEL);
	mutex_unlock(&dev->mutex);
	if (!*ion_virt)
		return -ENOMEM;

	return 0;
}

static void dmab_destroy(struct kref *kref)
{
	struct snd_dma_buffer_kref *dmab_kref =
			container_of(kref, struct snd_dma_buffer_kref, ref);
	struct snd_dma_buffer *dmab = &dmab_kref->dmab;
	struct device *dev = dmab->dev.dev;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	if (dmab->area) {
		pr_info("%s dmab_destroy\n", __func__);
		dat = dmab->addr;
		vaddr = dmab->area;
		size = dmab->bytes;
		dma_free_coherent(dev, size, vaddr, dat);
	}
	kfree(dmab_kref);
}

struct rtksnd_dma_buf_attachment {
	struct sg_table sgt;
};

static int rtksnd_dma_buf_attach(struct dma_buf *dmabuf,
				 struct dma_buf_attachment *attach)
{
	struct rtksnd_dma_buf_attachment *a;
	struct snd_dma_buffer_kref *dmab_kref = dmabuf->priv;
	struct snd_dma_buffer *dmab = &dmab_kref->dmab;
	struct device *dev = dmab->dev.dev;
	dma_addr_t daddr = dmab->addr;
	void *vaddr = dmab->area;
	size_t size = dmab->bytes;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dma_get_sgtable(dev, &a->sgt, vaddr, daddr, size);
	if (ret < 0) {
		dev_err(dev, "failed to get scatterlist from DMA API\n");
		kfree(a);
		return -EINVAL;
	}

	attach->priv = a;

	return 0;
}

static void rtksnd_dma_buf_detatch(struct dma_buf *dmabuf,
				   struct dma_buf_attachment *attach)
{
	struct rtksnd_dma_buf_attachment *a = attach->priv;

	sg_free_table(&a->sgt);
	kfree(a);
}

static struct sg_table *rtksnd_map_dma_buf(struct dma_buf_attachment *attach,
					   enum dma_data_direction dir)
{
	struct rtksnd_dma_buf_attachment *a = attach->priv;
	struct sg_table *table;
	int ret;

	table = &a->sgt;

	ret = dma_map_sgtable(attach->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);
	return table;
}

static void rtksnd_unmap_dma_buf(struct dma_buf_attachment *attach,
				 struct sg_table *table,
				 enum dma_data_direction dir)
{
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static int rtksnd_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct snd_dma_buffer_kref *dmab_kref = dmabuf->priv;
	struct snd_dma_buffer *dmab = &dmab_kref->dmab;
	struct device *dev = dmab->dev.dev;
	dma_addr_t daddr = dmab->addr;
	void *vaddr = dmab->area;
	size_t size = vma->vm_end - vma->vm_start;

	if (vaddr)
		return dma_mmap_coherent(dev, vma, vaddr, daddr, size);

	return 0;
}

static void rtksnd_release(struct dma_buf *dmabuf)
{
	struct snd_dma_buffer_kref *dmab_kref = dmabuf->priv;
	kref_put(&dmab_kref->ref, dmab_destroy);
}

static const struct dma_buf_ops rtksnd_dma_buf_ops = {
	.attach = rtksnd_dma_buf_attach,
	.detach = rtksnd_dma_buf_detatch,
	.map_dma_buf = rtksnd_map_dma_buf,
	.unmap_dma_buf = rtksnd_unmap_dma_buf,
	.mmap = rtksnd_mmap,
	.release = rtksnd_release,
};

static int snd_monitor_audio_data_queue_new(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	int audio_latency = 0;
	int retry = 0;
	u64 pcm_pts;
	u64 cur_pts;
	u64 diff_pts;
	u64 queuebuffer;
	unsigned int in_hwring_wp;
	unsigned int sum = 0;
	unsigned int latency = 0;
	unsigned int ptsl = 0;
	unsigned int ptsh = 0;
	unsigned int in_wp = 0;
	unsigned int wp_buffer;
	unsigned int wp_frame;

	if (dpcm->g_sharemem_ptr2) {
		/* to get alsa_latency_info from fw */
		if (dpcm->g_sharemem_ptr3) {
			memcpy(dpcm->g_sharemem_ptr3,
			       dpcm->g_sharemem_ptr2,
			       sizeof(struct alsa_latency_info));

			latency = dpcm->g_sharemem_ptr3->latency;
			ptsl = dpcm->g_sharemem_ptr3->ptsl;
			ptsh = dpcm->g_sharemem_ptr3->ptsh;
			sum = dpcm->g_sharemem_ptr3->sum;

			/* If the sync word is set, wp is physical address */
			if (dpcm->g_sharemem_ptr3->sync == 0x23792379)
				in_wp = dpcm->g_sharemem_ptr3->decin_wp;
			else
				in_wp = dpcm->g_sharemem_ptr3->decin_wp & 0x0fffffff;

			/* make sure all of alsa_latency_info are updated. */
			while (sum != (latency + ptsl)) {
				if (retry > 100) {
					if (ptsl < sum)
						latency = sum - ptsl;
					break;
				}
				memcpy(dpcm->g_sharemem_ptr3,
				       dpcm->g_sharemem_ptr2,
				       sizeof(struct alsa_latency_info));

				latency = dpcm->g_sharemem_ptr3->latency;
				ptsl = dpcm->g_sharemem_ptr3->ptsl;
				ptsh = dpcm->g_sharemem_ptr3->ptsh;
				sum = dpcm->g_sharemem_ptr3->sum;

				/* If the sync word is set, wp is physical address */
				if (dpcm->g_sharemem_ptr3->sync == 0x23792379)
					in_wp = dpcm->g_sharemem_ptr3->decin_wp;
				else
					in_wp = dpcm->g_sharemem_ptr3->decin_wp & 0x0fffffff;

				retry++;
			}
		}

		/* Physical address */
		in_hwring_wp = dpcm->dec_inring->write_ptr;

		if (retry > 20) {
			pr_info("latency 0x%x, ptsl 0x%x, ptsh 0x%x, sum 0x%x, ",
				latency, ptsl, ptsh, sum);
			pr_info("in_wp 0x%x in_hwring_wp 0x%x\n",
				in_wp, in_hwring_wp);
		}

		pcm_pts = (((u64)ptsh << 32) | ((u64)ptsl));
		cur_pts = rtk_get_90k_pts_hifi();
		diff_pts = cur_pts - pcm_pts;

		wp_frame = 0;
		if (in_wp != in_hwring_wp) {
			wp_buffer = ring_valid_data(dpcm->dec_inring->begin_addr,
						    dpcm->dec_inring->begin_addr +
						    dpcm->dec_inring->size,
						    in_wp, in_hwring_wp);
			wp_frame = bytes_to_frames(runtime, wp_buffer);
		}

		/* old version afw only has latency without ptsl and wp */
		if (in_wp == 0 && ptsl == 0) {
			if (dpcm->g_sharemem_ptr) {
				audio_latency = *dpcm->g_sharemem_ptr;
				audio_latency += dpcm->dec_out_msec;
			}
		} else {
			queuebuffer = wp_frame +
					ring_valid_data(0, runtime->boundary,
							dpcm->total_write,
							runtime->control->appl_ptr);

			queuebuffer = div_u64(queuebuffer * 1000000, runtime->rate);
			audio_latency = latency + queuebuffer - div64_ul(diff_pts * 1000, 90);
			audio_latency = audio_latency / 1000;
		}

		if (audio_latency < 0)
			audio_latency = 0;

	} else if (dpcm->g_sharemem_ptr) {
		audio_latency = *dpcm->g_sharemem_ptr;
		audio_latency += dpcm->dec_out_msec;
	} else {
		pr_err("NO exist share memory !!\n");
	}

	mtotal_latency = audio_latency;

	return audio_latency;
}

int rtk_snd_capture_hdmirx_enable(void)
{
#define HDMI_RX_CLK_BIT 24
	int ret = 0;

	if (sys_clk_en2_virt) {
		ret = (int)readl(sys_clk_en2_virt);
		ret = (ret >> HDMI_RX_CLK_BIT) & 0x1;
	}

	return ret;
}

static int rtk_snd_init_ringheader_AI(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ring_header;
	int ret, ch;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;

	for (ch = 0; ch < runtime->channels; ch++) {
		ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
				    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
				    RTK_FLAG_HIFIACC);
		if (ret) {
			pr_err("%s alloc mem fail\n", __func__);
			return ret;
		}

		dpcm->size_airing[ch] = size;
		dpcm->phy_airing[ch] = dat;
		dpcm->airing[ch] = vaddr;
	}

	/* init ring header */
	for (ch = 0; ch < runtime->channels; ++ch) {
		dpcm->airing[ch]->begin_addr = (unsigned long)dpcm->phy_airing_data[ch];
		dpcm->airing[ch]->size = dpcm->ring_size;
		dpcm->airing[ch]->read_ptr[0] = dpcm->airing[ch]->begin_addr;
		dpcm->airing[ch]->write_ptr = dpcm->airing[ch]->begin_addr;
		dpcm->airing[ch]->num_read_ptr = 1;

		ring_header.ringbuffer_header_list[ch] = (unsigned long)dpcm->phy_airing[ch];
	}

	/* init AI ring header */
	ring_header.instance_id = dpcm->ai_agent_id;
	ring_header.pin_id = PCM_OUT;
	ring_header.read_idx = -1;
	ring_header.list_size =  runtime->channels;

	/* RPC set AI ring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s fail]\n", __func__);
		return -1;
	}

	return 0;
}

static int rtk_snd_capture_LPCM_ringheader_AI(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ring_header;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_HIFIACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_lpcm_ring = dat;
	dpcm->lpcm_ring = vaddr;

	/* init ring header */
	dpcm->lpcm_ring->begin_addr = (unsigned int)dpcm->phy_lpcm_data;
	dpcm->lpcm_ring->size = dpcm->lpcm_ring_size;
	dpcm->lpcm_ring->read_ptr[0] = dpcm->lpcm_ring->begin_addr;
	dpcm->lpcm_ring->write_ptr = dpcm->lpcm_ring->begin_addr;
	dpcm->lpcm_ring->num_read_ptr = 1;

	ring_header.ringbuffer_header_list[0] = (unsigned long)dpcm->phy_lpcm_ring;

	/* init AI ring header */
	ring_header.instance_id = dpcm->ai_agent_id;
	ring_header.read_idx = -1;
	ring_header.list_size = 1;
	ring_header.pin_id = BASE_BS_OUT;

	/* RPC set AI ring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc,
				      dpcm->vaddr_rpc,
				      &ring_header,
				      ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int rtk_snd_capture_PTS_ringheader_AI(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ring_header;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_HIFIACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_pts_ring_hdr = dat;
	dpcm->pts_ring_hdr = vaddr;

	dpcm->pts_ring.base = (unsigned long)dpcm->pts_mem.p_virt;
	dpcm->pts_ring.wp = dpcm->pts_ring.base;
	dpcm->pts_ring.rp = dpcm->pts_ring.base;
	dpcm->pts_ring.limit = dpcm->pts_ring.base + dpcm->pts_mem.size;

	/* init ring header */
	dpcm->pts_ring_hdr->begin_addr = (unsigned int)dpcm->pts_mem.p_phy;
	dpcm->pts_ring_hdr->size = dpcm->pts_mem.size;
	dpcm->pts_ring_hdr->read_ptr[0] = dpcm->pts_ring_hdr->begin_addr;
	dpcm->pts_ring_hdr->write_ptr = dpcm->pts_ring_hdr->begin_addr;
	dpcm->pts_ring_hdr->num_read_ptr = 1;

	ring_header.ringbuffer_header_list[0] = (unsigned long)dpcm->phy_pts_ring_hdr;

	/* init AI ring header */
	ring_header.instance_id = dpcm->ai_agent_id;
	ring_header.pin_id = MESSAGE_QUEUE;
	ring_header.read_idx = -1;
	ring_header.list_size = 1;

	/* RPC set AI ring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int rtk_snd_init_ringheader_DEC_AO(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ring_header;
	int ch, j, ret;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;

	for (ch = 0; ch < runtime->channels; ch++) {
		ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
				    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
				    RTK_FLAG_HIFIACC);
		if (ret) {
			pr_err("%s alloc mem fail\n", __func__);
			return ret;
		}

		dpcm->phy_dec_out_ring[ch] = dat;
		dpcm->dec_out_ring[ch] = vaddr;
	}

	/* init ring header */
	for (ch = 0; ch < runtime->channels ; ++ch) {
		dpcm->dec_out_ring[ch]->begin_addr = (unsigned int)dpcm->phy_dec_out_data[ch];
		dpcm->dec_out_ring[ch]->size = dpcm->ring_size;
		for (j = 0 ; j < 4 ; ++j)
			dpcm->dec_out_ring[ch]->read_ptr[j] = dpcm->dec_out_ring[ch]->begin_addr;
		dpcm->dec_out_ring[ch]->write_ptr = dpcm->dec_out_ring[ch]->begin_addr;
		dpcm->dec_out_ring[ch]->num_read_ptr = 1;
	}

	/* init DEC outring header */
	ring_header.instance_id = dpcm->dec_agent_id;
	ring_header.pin_id = PCM_OUT;
	for (ch = 0 ; ch < runtime->channels ; ++ch)
		ring_header.ringbuffer_header_list[ch] = (unsigned long)dpcm->phy_dec_out_ring[ch];
	ring_header.read_idx = -1;
	ring_header.list_size = runtime->channels;

	/* RPC set DEC outring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	/* init AO inring header */
	ring_header.instance_id = dpcm->ao_agent_id;
	ring_header.pin_id = dpcm->ao_pin_id;
	for (ch = 0 ; ch < runtime->channels ; ++ch)
		ring_header.ringbuffer_header_list[ch] = (unsigned long)dpcm->phy_dec_out_ring[ch];
	ring_header.read_idx = 0;
	ring_header.list_size = runtime->channels;

	/* RPC set AO inring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int rtk_snd_init_ringheader_AO(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ring_header;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_HIFIACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_dec_inring = dat;
	dpcm->dec_inring = vaddr;

	dpcm->dec_inring->begin_addr = (unsigned int)runtime->dma_addr;
	dpcm->dec_inring->buffer_id = RINGBUFFER_STREAM;
	dpcm->dec_inring->size = frames_to_bytes(runtime, runtime->buffer_size);
	dpcm->dec_inring->write_ptr = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[0] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[1] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[2] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[3] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->num_read_ptr = 1;

	/* init AO inring header */
	ring_header.instance_id = dpcm->ao_agent_id;
	ring_header.pin_id = dpcm->ao_pin_id;
	ring_header.ringbuffer_header_list[0] = (unsigned long)dpcm->phy_dec_inring;
	ring_header.read_idx = 0;
	ring_header.list_size = 1;

	/* RPC set AO inring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int rtk_snd_init_AO_ringheader_by_AI(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct audio_rpc_ringbuffer_header ring_header;
	int ch;

	/* In DMIC PASSTHROUGH, the ao uses the ai Ringheader
	 * Therefore, we don't need set up again.
	 */
	for (ch = 0; ch < runtime->channels; ++ch)
		ring_header.ringbuffer_header_list[ch] = (unsigned long)dpcm->phy_airing[ch];

	/* init AO ring header */
	ring_header.instance_id = dpcm->ao_agent_id;
	ring_header.pin_id = dpcm->ao_pin_id;
	ring_header.read_idx = 0;
	ring_header.list_size = runtime->channels;

	/* RPC set AO ring header */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ring_header, ring_header.list_size) < 0) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int rtk_snd_init_connect(phys_addr_t paddr,
				void *vaddr,
				int src_instance_id,
				int des_instance_id,
				int des_pin_id)
{
	struct audio_rpc_connection connection;

	connection.des_instance_id = des_instance_id;
	connection.src_instance_id = src_instance_id;
	connection.src_pin_id = PCM_OUT;
	connection.des_pin_id = des_pin_id;

	if (rpc_connect_svc(paddr, vaddr, &connection)) {
		pr_err("[%s fail]\n", __func__);
		return -1;
	}

	return 0;
}

static int rtk_snd_init_decoder_inring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	struct audio_rpc_ringbuffer_header ringbuf_header;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_HIFIACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_dec_inring = dat;
	dpcm->dec_inring = vaddr;

	/* init HW inring header */
	dpcm->dec_inring->begin_addr = (unsigned int)runtime->dma_addr;
	dpcm->dec_inring->buffer_id = RINGBUFFER_STREAM;
	dpcm->dec_inring->size = frames_to_bytes(runtime, runtime->buffer_size);
	dpcm->dec_inring->write_ptr = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[0] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[1] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[2] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->read_ptr[3] = dpcm->dec_inring->begin_addr;
	dpcm->dec_inring->num_read_ptr = 1;

	/* init RPC ring header */
	ringbuf_header.instance_id = dpcm->dec_agent_id;
	ringbuf_header.pin_id = BASE_BS_IN;
	ringbuf_header.ringbuffer_header_list[0] = (unsigned long)dpcm->phy_dec_inring;
	ringbuf_header.read_idx = 0;
	ringbuf_header.list_size = 1;

	/* RPC set decoder in_ring */
	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ringbuf_header, ringbuf_header.list_size)) {
		pr_err("[fail %s %d]\n", __func__, __LINE__);
		return -1;
	}

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_HIFIACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_dec_inband_ring = dat;
	dpcm->dec_inband_ring = vaddr;

	/* init inband ring header */
	dpcm->dec_inband_ring->begin_addr = (int)((unsigned long)dpcm->dec_inband_data -
				(unsigned long)dpcm + dpcm->phy_addr);
	dpcm->dec_inband_ring->size = sizeof(dpcm->dec_inband_data);
	dpcm->dec_inband_ring->read_ptr[0] = dpcm->dec_inband_ring->begin_addr;
	dpcm->dec_inband_ring->write_ptr = dpcm->dec_inband_ring->begin_addr;
	dpcm->dec_inband_ring->num_read_ptr = 1;

	/* init RPC ring header */
	ringbuf_header.instance_id = dpcm->dec_agent_id;
	ringbuf_header.pin_id = INBAND_QUEUE;
	ringbuf_header.ringbuffer_header_list[0] =
				(unsigned long)dpcm->phy_dec_inband_ring;
	ringbuf_header.read_idx = 0;
	ringbuf_header.list_size = 1;

	if (rpc_initringbuffer_header(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				      &ringbuf_header, ringbuf_header.list_size)) {
		pr_err("[fail %s %d]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int snd_realtek_hw_capture_run(struct snd_rtk_cap_pcm *dpcm)
{
	if (dpcm->source_in == ENUM_AIN_DMIC_PASSTHROUGH) {
		if (rpc_pause_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				  dpcm->ao_agent_id | dpcm->ao_pin_id)) {
			pr_err("[%s fail]\n", __func__);
			return -1;
		}
	}

	if (rpc_pause_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			  dpcm->ai_agent_id)) {
		pr_err("[%s fail]\n", __func__);
		return -1;
	}

	if (dpcm->source_in == ENUM_AIN_DMIC_PASSTHROUGH) {
		if (rpc_run_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				dpcm->ao_agent_id | dpcm->ao_pin_id)) {
			pr_err("[%s fail]\n", __func__);
			return -1;
		}
	}

	if (rpc_run_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			dpcm->ai_agent_id)) {
		pr_err("[%s fail]\n", __func__);
		return -1;
	}

	return 0;
}

static int snd_realtek_hw_resume(struct snd_rtk_pcm *dpcm)
{
	pr_info("[%s %s %d]\n", __FILE__, __func__, __LINE__);

	if (dpcm->ao_decode_lpcm) {
		if (rpc_run_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				dpcm->dec_agent_id)) {
			pr_err("[%s %d]\n", __func__, __LINE__);
			return -1;
		}
	}

	if (rpc_run_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			dpcm->ao_agent_id | dpcm->ao_pin_id)) {
		pr_err("[%s %d]\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

int write_inband_cmd(struct snd_rtk_pcm *dpcm, void *data, int len)
{
	unsigned long base, limit, wp;

	base = (unsigned long)dpcm->dec_inband_data;
	limit = base + sizeof(dpcm->dec_inband_data);
	wp = base + (unsigned long)(dpcm->dec_inband_ring->write_ptr -
				dpcm->dec_inband_ring->begin_addr);
	wp = buf_memcpy2_ring(base, limit, wp, (char *)data, (unsigned long)len);
	dpcm->dec_inband_ring->write_ptr = (int)(wp - base) + dpcm->dec_inband_ring->begin_addr;

	return len;
}

static unsigned int rtk_channel_mapping(int num_channels)
{
	unsigned int channel_mapping_info;

	/* Set up channel mapping info for AFW */
	switch (num_channels) {
	case 1:
		channel_mapping_info = (0x1) << 6;
		break;
	case 2:
		channel_mapping_info = (0x3) << 6;
		break;
	case 4:
		channel_mapping_info = (0x33) << 6;
		break;
	case 6:
		channel_mapping_info = (0x3F) << 6;
		break;
	case 8:
		channel_mapping_info = (0x63F) << 6;
		break;
	default:
		channel_mapping_info = (0x3) << 6;
		break;
	}

	return channel_mapping_info;
}

static int rtk_snd_init_decoder_info(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct audio_dec_new_format cmd;
	struct audio_rpc_sendio sendio;

	pr_info("[%s %s %d]\n", __FILE__, __func__, __LINE__);

	/* decoder pause */
	if (rpc_stop_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			 dpcm->dec_agent_id)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	/* decoder flush */
	sendio.instance_id = dpcm->dec_agent_id;
	sendio.pin_id = dpcm->dec_pin_id;
	if (rpc_flush_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			  &sendio)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	/* decoder run */
	if (rpc_run_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			dpcm->dec_agent_id)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	cmd.audio_type = AUDIO_LPCM_DECODER_TYPE;
	cmd.header.type = AUDIO_DEC_INBAMD_CMD_TYPE_NEW_FORMAT;
	cmd.header.size = sizeof(struct audio_dec_new_format);
	cmd.private_info[0] = runtime->channels;
	cmd.private_info[1] = runtime->sample_bits;
	cmd.private_info[2] = runtime->rate;
	cmd.private_info[3] = 0;

	/* private_info[4]&0x3FFFC0)>>6; bit[6:21] for wave channel mask */
	cmd.private_info[4] = rtk_channel_mapping(runtime->channels);
	cmd.private_info[5] = 0;

	/* (a_format->privateData[0]&0x1)<<4 : float or not */
	cmd.private_info[6] = 0;
	cmd.private_info[7] = 0;

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_BE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_U24_3BE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_U20_3BE:
	case SNDRV_PCM_FORMAT_S18_3BE:
	case SNDRV_PCM_FORMAT_U18_3BE:
	case SNDRV_PCM_FORMAT_FLOAT_BE:
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		cmd.private_info[7]  = AUDIO_BIG_ENDIAN;
		break;
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_U18_3LE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_MU_LAW:
	case SNDRV_PCM_FORMAT_A_LAW:
	case SNDRV_PCM_FORMAT_IMA_ADPCM:
	case SNDRV_PCM_FORMAT_MPEG:
	case SNDRV_PCM_FORMAT_GSM:
	case SNDRV_PCM_FORMAT_SPECIAL:
	default:
		cmd.private_info[7]  = AUDIO_LITTLE_ENDIAN;
		break;
	}

	cmd.w_ptr = dpcm->dec_inring[0].write_ptr;

	/* write decoder info into inband of decoder */
	write_inband_cmd(dpcm, &cmd, sizeof(struct audio_dec_new_format));

	return 0;
}

static int snd_realtek_reprepare(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct audio_rpc_sendio sendio;

	rpc_put_share_memory_latency(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				     NULL, NULL, 0, dpcm->ao_pin_id,
				     ENUM_PRIVATEINFO_AUDIO_GET_SHARE_MEMORY_FROM_ALSA);

	if (dpcm->ao_decode_lpcm) {
		if (rpc_stop_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				 dpcm->dec_agent_id)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -1;
		}
	}

	if (rpc_pause_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			  dpcm->ao_agent_id | dpcm->ao_pin_id)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	if (rpc_stop_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			 dpcm->ao_agent_id | dpcm->ao_pin_id)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	if (dpcm->ao_decode_lpcm) {
		/* decoder flush */
		sendio.instance_id = dpcm->dec_agent_id;
		sendio.pin_id = dpcm->dec_pin_id;
		if (rpc_flush_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				  &sendio)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -1;
		}

		/* destroy decoder */
		if (rpc_destroy_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				    dpcm->dec_agent_id)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -1;
		}
	}

	/* clear decoder InBand ring */
	memset(dpcm->dec_inband_data, 0, sizeof(unsigned int) * 64);

	/* free ao in_ring and decoder out_ring */
	snd_realtek_hw_free_ring(runtime);

	return 0;
}

static int rtk_snd_capture_malloc_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	int ch, ret;
	phys_addr_t dat;
	void *vaddr;
	size_t size = RTK_ENC_AI_BUFFER_SIZE;

	for (ch = 0; ch < runtime->channels ; ++ch) {
		if (dpcm->vaddr_airing_data[ch]) {
			pr_err("[re-malloc: %s]\n", __func__);

			vaddr = dpcm->vaddr_airing_data[ch];
			dat = dpcm->phy_airing_data[ch];
			size = dpcm->ring_size;
			dma_free_coherent(dev, size, vaddr, dat);
		}

		if (dpcm->source_in == ENUM_AIN_I2S_LOOPBACK) {
			ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
					    RTK_FLAG_PROTECTED_V2_AO_POOL | RTK_FLAG_SCPUACC |
					    RTK_FLAG_ACPUACC);
		} else {
			ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
					    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
					    RTK_FLAG_ACPUACC);
		}

		if (ret) {
			pr_err("%s alloc mem fail\n", __func__);
			return ret;
		}

		dpcm->vaddr_airing_data[ch] = vaddr;
		dpcm->phy_airing_data[ch] = dat;
		dpcm->ring_size = size;
	}

	return 0;
}

static int rtk_snd_capture_malloc_lpcm_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	phys_addr_t dat;
	void *vaddr;
	size_t size = RTK_ENC_LPCM_BUFFER_SIZE;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->phy_lpcm_data = dat;
	dpcm->lpcm_data = vaddr;
	dpcm->lpcm_ring_size = size;

	return 0;
}

static int rtk_snd_capture_malloc_pts_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	phys_addr_t dat;
	void *vaddr;
	size_t size = RTK_ENC_PTS_BUFFER_SIZE;
	int ret;

	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		pr_err("%s alloc mem fail\n", __func__);
		return ret;
	}

	dpcm->pts_mem.size = size;
	dpcm->pts_mem.p_phy = dat;
	dpcm->pts_mem.p_virt = vaddr;

	return 0;
}

static int rtk_snd_malloc_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	int ch, ret;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	for (ch = 0; ch < runtime->channels; ++ch) {
		size = rtk_dec_ao_buffer;
		ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
				    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
				    RTK_FLAG_ACPUACC);
		if (ret) {
			pr_err("%s alloc mem fail\n", __func__);
			return ret;
		}

		dpcm->vaddr_dec_out_data[ch] = vaddr;
		dpcm->phy_dec_out_data[ch] = dat;
		dpcm->ring_size = size;
	}

	return 0;
}

static int snd_realtek_hw_create_AI(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;

	if (snd_open_ai_count >= MAX_AI_DEVICES) {
		pr_err("[too more AI %s]\n", __func__);
		return -1;
	}

	if (rpc_create_ai_agent(dpcm->phy_addr_rpc, dpcm->vaddr_rpc, dpcm)) {
		pr_err("[err %s]\n", __func__);
		return -1;
	}

	snd_open_ai_count++;

	return 0;
}

static int snd_realtek_hw_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;

	if (snd_open_count >= MAX_PCM_SUBSTREAMS) {
		pr_err("[opened audio stream count excess %d]\n", MAX_PCM_SUBSTREAMS);
		return -1;
	}

	/* get ao flash pin ID */
	dpcm->ao_pin_id = rpc_get_ao_flash_pin(dpcm->phy_addr_rpc,
					       dpcm->vaddr_rpc, dpcm->ao_agent_id);
	if (dpcm->ao_pin_id < 0) {
		pr_err("[can't get flash pin %s %d]\n", __func__, __LINE__);
		return -1;
	}

	/* init volume */
	dpcm->volume = 31;

	snd_open_count++;

	return 0;
}

static int snd_realtek_hw_init(struct device *dev,
			       struct snd_rtk_pcm *dpcm)
{
	if (dpcm->hw_init != 0)
		return 0;

	if (rpc_create_ao_agent(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				&dpcm->ao_agent_id, AUDIO_ALSA_OUT)) {
		pr_err("[No AO agent %s %d]\n", __func__, __LINE__);
		return -1;
	}

	dpcm->hw_init = 1;

	return 0;
}

static int snd_realtek_hw_capture_free_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	int ch;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	pr_info("[ALSA %s]\n", __func__);

	/* free AI in_ring */
	for (ch = 0; ch < runtime->channels; ch++) {
		if (dpcm->vaddr_airing_data[ch]) {
			vaddr = dpcm->vaddr_airing_data[ch];
			dat = dpcm->phy_airing_data[ch];
			size = dpcm->ring_size;
			dma_free_coherent(dev, size, vaddr, dat);
			dpcm->vaddr_airing_data[ch] = NULL;
		}
	}

	for (ch = 0; ch < runtime->channels; ch++) {
		if (dpcm->airing[ch]) {
			vaddr = dpcm->airing[ch];
			dat = dpcm->phy_airing[ch];
			size = dpcm->size_airing[ch];
			dma_free_coherent(dev, size, vaddr, dat);
			dpcm->airing[ch] = NULL;
		}
	}

	/* free AI lpcm ring */
	if (dpcm->lpcm_data) {
		dat = dpcm->phy_lpcm_data;
		vaddr = dpcm->lpcm_data;
		size = dpcm->lpcm_ring_size;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->lpcm_data = NULL;
	}

	if (dpcm->lpcm_ring) {
		dat = dpcm->phy_lpcm_ring;
		vaddr = dpcm->lpcm_ring;
		size = SZ_4K;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->lpcm_ring = NULL;
	}

	/* free AI pts ring */
	if (dpcm->pts_mem.p_virt) {
		size = dpcm->pts_mem.size;
		dat = dpcm->pts_mem.p_phy;
		vaddr = dpcm->pts_mem.p_virt;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->pts_mem.p_virt = NULL;
	}

	if (dpcm->pts_ring_hdr) {
		dat = dpcm->phy_pts_ring_hdr;
		vaddr = dpcm->pts_ring_hdr;
		size = SZ_4K;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->pts_ring_hdr = NULL;
	}

	return 0;
}

static int snd_realtek_hw_free_ring(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = dpcm->substream->pcm->card->dev;
	int ch;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	for (ch = 0; ch < dpcm->last_channel; ch++) {
		if (dpcm->vaddr_dec_out_data[ch]) {
			vaddr = dpcm->vaddr_dec_out_data[ch];
			dat = dpcm->phy_dec_out_data[ch];
			size = dpcm->ring_size;
			dma_free_coherent(dev, size, vaddr, dat);
			dpcm->vaddr_dec_out_data[ch] = NULL;
		}
	}

	for (ch = 0; ch < dpcm->last_channel; ch++) {
		if (dpcm->dec_out_ring[ch]) {
			dat = dpcm->phy_dec_out_ring[ch];
			vaddr = dpcm->dec_out_ring[ch];
			size = SZ_4K;
			dma_free_coherent(dev, size, vaddr, dat);
			dpcm->dec_out_ring[ch] = NULL;
		}
	}

	if (dpcm->dec_inring) {
		dat = dpcm->phy_dec_inring;
		vaddr = dpcm->dec_inring;
		size = SZ_4K;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->dec_inring = NULL;
	}

	if (dpcm->dec_inband_ring) {
		dat = dpcm->phy_dec_inband_ring;
		vaddr = dpcm->dec_inband_ring;
		size = SZ_4K;
		dma_free_coherent(dev, size, vaddr, dat);
		dpcm->dec_inband_ring = NULL;
	}

	return 0;
}

static void snd_card_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	if (!dpcm)
		goto out;

	dev = dpcm->substream->pcm->card->dev;

	pr_info("%s free playback dma buf\n", __func__);
	size = sizeof(struct snd_rtk_pcm);
	dat = dpcm->phy_addr;
	vaddr = dpcm;
	dma_free_coherent(dev, size, vaddr, dat);
out:
	runtime->private_data = NULL;
}

static void snd_card_capture_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	if (!dpcm)
		goto out;

	dev = dpcm->substream->pcm->card->dev;

	pr_info("%s free capture dma buf\n", __func__);
	size = sizeof(struct snd_rtk_cap_pcm);
	dat = dpcm->phy_addr;
	vaddr = dpcm;
	dma_free_coherent(dev, size, vaddr, dat);
out:
	runtime->private_data = NULL;
}

static int
hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	struct snd_pcm *pcm = hwdep->private_data;
	struct snd_rtk_pcm *dpcm = NULL;
	struct snd_rtk_cap_pcm *dpcm_c = NULL;
	struct snd_pcm_mmap_fd __user *_mmap_fd = NULL;
	struct snd_pcm_mmap_fd mmap_fd;
	struct snd_pcm_substream *substream = NULL;
	struct snd_dma_buffer *dmab = NULL;
	struct snd_dma_buffer_kref *dmab_kref = NULL;
	struct dma_buf *dmabuf = NULL;
	int ret = 0, mlatency = 0, delay_ms;
	s32 dir = -1;
	unsigned int volume;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	switch (cmd) {
	case SNDRV_PCM_IOCTL_MMAP_DATA_FD:
		_mmap_fd = (struct snd_pcm_mmap_fd __user *)arg;
		if (get_user(dir, (s32 __user *)&_mmap_fd->dir)) {
			pr_err("%s: error copying mmap_fd from user\n",
			       __func__);
			ret = -EFAULT;
			break;
		}

		if (dir != SNDRV_PCM_STREAM_PLAYBACK &&
		    dir != SNDRV_PCM_STREAM_CAPTURE) {
			pr_err("%s invalid stream dir: %d\n", __func__, dir);
			ret = -EINVAL;
			break;
		}

		substream = pcm->streams[dir].substream;
		if (!substream || !substream->runtime) {
			pr_err("%s substream or runtime not found\n", __func__);
			ret = -ENODEV;
			break;
		}

		pr_debug("%s : %s MMAP Data fd\n", __func__,
			 dir == 0 ? "P" : "C");

		dmab = substream->runtime->dma_buffer_p;
		if (!dmab) {
			pr_err("%s dmab not found\n", __func__);
			ret = -ENODEV;
			break;
		}
		dmab_kref = container_of(dmab, struct snd_dma_buffer_kref, dmab);

		exp_info.ops = &rtksnd_dma_buf_ops;
		exp_info.size = dmab->bytes;
		exp_info.flags = O_RDWR;
		exp_info.priv = dmab_kref;

		kref_get(&dmab_kref->ref);
		dmabuf = dma_buf_export(&exp_info);
		if (IS_ERR(dmabuf)) {
			pr_err("%s dmabuf export fail\n", __func__);
			kref_put(&dmab_kref->ref, dmab_destroy);
			return -EFAULT;
		}

		mmap_fd.fd = dma_buf_fd(dmabuf, O_CLOEXEC);
		if (mmap_fd.fd < 0) {
			pr_info("%s %d Get mmap buffer fd fail.\n", __func__, __LINE__);
			dma_buf_put(dmabuf);
			return -EIO;
		}

		if (put_user(mmap_fd.fd, &_mmap_fd->fd)) {
			pr_err("%s: error copying fd\n", __func__);
			return -EFAULT;
		}
		break;
	case SNDRV_PCM_IOCTL_GET_LATENCY:
		substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
		if (!substream || !substream->runtime) {
			pr_err("%s substream or runtime not found\n", __func__);
			ret = -ENODEV;
			break;
		}

		dpcm = substream->runtime->private_data;
		if (dpcm && dpcm->init_ring)
			mlatency = snd_monitor_audio_data_queue_new(substream);

		put_user(mlatency, (int __user *)arg);
		break;
	case SNDRV_PCM_IOCTL_GET_FW_DELAY:
		substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
		if (!substream || !substream->runtime) {
			pr_err("%s substream or runtime not found\n", __func__);
			ret = -ENODEV;
			break;
		}

		delay_ms = snd_capture_monitor_delay(substream);
		put_user(delay_ms, (unsigned int __user *)arg);
		break;
	case SNDRV_PCM_IOCTL_DMIC_VOL_SET:
		if (pcm->device != 9) {
			pr_err("%s This ioctl only for dmic\n", __func__);
			ret = -ENODEV;
			break;
		}

		substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
		if (!substream || !substream->runtime) {
			pr_err("%s substream or runtime not found\n", __func__);
			ret = -ENODEV;
			break;
		}

		dpcm_c = substream->runtime->private_data;
		if (!dpcm_c) {
			pr_err("%s dpcm_c not found\n", __func__);
			ret = -ENODEV;
			break;
		}

		get_user(volume, (unsigned int __user *)arg);
		dpcm_c->dmic_volume[0] = volume / 10; /* dmic volume x */
		dpcm_c->dmic_volume[1] = volume % 10; /* dmic volume y */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static int
hwdep_compat_ioctl(struct snd_hwdep *hwdep, struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	return hwdep_ioctl(hwdep, file, cmd,
			   (unsigned long)compat_ptr(arg));
}
#else
#define hwdep_compat_ioctl NULL
#endif

static int snd_rtk_create_hwdep_device(struct snd_pcm *pcm)
{
	static const struct snd_hwdep_ops ops = {
		.ioctl		= hwdep_ioctl,
		.ioctl_compat	= hwdep_compat_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(pcm->card,
			    pcm->name, pcm->device, &hwdep);
	if (err < 0)
		goto end;
	strncpy(hwdep->name, pcm->name, sizeof(hwdep->name) - 1);
	hwdep->iface = pcm->device;
	hwdep->ops = ops;
	hwdep->private_data = pcm;
	hwdep->exclusive = true;
end:
	return err;
}

static int snd_card_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = substream->pcm->card->dev;
	struct snd_rtk_pcm *dpcm = NULL;
	int ret = 0;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	pr_info("[ALSA %s %s]\n", substream->pcm->name, __func__);

	size = sizeof(struct snd_rtk_pcm);
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	dpcm = vaddr;
	memset(dpcm, 0, sizeof(struct snd_rtk_pcm));
	dpcm->phy_addr = dat;

	/* Preparing a memory for all rpc */
	size = SZ_4K;
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail_rpc;
	}

	dpcm->phy_addr_rpc = dat;
	dpcm->vaddr_rpc = vaddr;

	runtime->private_data = dpcm;
	runtime->private_free = snd_card_runtime_free;

	/* check if AO exists */
	if (snd_realtek_hw_init(dev, dpcm)) {
		pr_err("[error %s %d]\n", __func__, __LINE__);
		ret = -EIO;
		goto fail_dat;
	}

	memcpy(&runtime->hw, &snd_card_playback, sizeof(struct snd_pcm_hardware));

	dpcm->substream = substream;

	if (snd_realtek_hw_open(substream) < 0) {
		pr_err("[error %s %d]\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto fail_dat;
	}

	dpcm->mixer = (struct rtk_snd_mixer *)(substream->pcm->card->private_data);

	/* init dec_out_msec */
	dpcm->dec_out_msec = 0;

	pr_info("ALSA: Device number is %d\n", substream->pcm->device);
	switch (substream->pcm->device) {
	case 0:
		/* AO not decode lpcm */
		dpcm->ao_decode_lpcm = 0;
		pr_info("ALSA: AO skip decoder normal flow.\n");
		break;
	case 1:
		dpcm->ao_decode_lpcm = 0;
		pr_info("ALSA: AO skip decoder mmap flow.\n");
		break;
	case 2:
		dpcm->ao_decode_lpcm = 0;
		pr_info("ALSA: AO skip decoder deepbuffer flow.\n");
		break;
	case 3:
		/* AO decode lpcm */
		dpcm->ao_decode_lpcm = 1;
		pr_info("ALSA: AO with decoder flow.\n");
		break;
	default:
		pr_info("ALSA: Unknown pcm number.\n");
		break;
	}

#ifdef DEBUG_RECORD
	/* Create the file needs to after initalizing the sysem. */
	dpcm->fp = filp_open("/mnt/debug_record.wav", O_RDWR | O_CREAT, 0644);
	if (IS_ERR(dpcm->fp)) {
		pr_err("[create file error %s %d dpcm->fp %d]\n", __func__, __LINE__, PTR_ERR(dpcm->fp));
		dpcm->pos = -1;
	} else {
		dpcm->pos = 0;
	}
#endif

	spin_lock_init(&dpcm->playback_lock);

	dpcm->mixer->mixer_volume[MIXER_ADDR_MASTER][0] =
	dpcm->mixer->mixer_volume[MIXER_ADDR_MASTER][1] =
	rpc_get_volume(dpcm->phy_addr_rpc, dpcm->vaddr_rpc);

	/* Preparing the ion buffer for share memory 1 */
	size = SZ_4K;
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail_dat;
	}

	dpcm->g_sharemem_ptr_dat = dat;
	dpcm->g_sharemem_ptr = vaddr;

	memset(dpcm->g_sharemem_ptr, 0, 4096);

	/* Preparing the ion buffer for share memory 2 */
	size = sizeof(struct alsa_latency_info);
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail_dat2;
	}

	dpcm->g_sharemem_ptr_dat2 = dat;
	dpcm->g_sharemem_ptr2 = vaddr;

	memset(dpcm->g_sharemem_ptr2, 0, sizeof(struct alsa_latency_info));

	/* Set up the sync word for new latency structure */
	dpcm->g_sharemem_ptr2->sync = 0x23792379;

	/* Preparing the ion buffer for share memory 3 */
	size = sizeof(struct alsa_latency_info);
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail_dat3;
	}

	dpcm->g_sharemem_ptr_dat3 = dat;
	dpcm->g_sharemem_ptr3 = vaddr;

	memset(dpcm->g_sharemem_ptr3, 0, sizeof(struct alsa_latency_info));

	/* Set up the sync word for new latency structure */
	dpcm->g_sharemem_ptr3->sync = 0x23792379;

	return ret;
fail_dat3:
	size = sizeof(struct alsa_latency_info);
	dat = dpcm->g_sharemem_ptr_dat2;
	vaddr = dpcm->g_sharemem_ptr2;
	dma_free_coherent(dev, size, vaddr, dat);
fail_dat2:
	size = SZ_4K;
	dat = dpcm->g_sharemem_ptr_dat;
	vaddr = dpcm->g_sharemem_ptr;
	dma_free_coherent(dev, size, vaddr, dat);
fail_dat:
	size = SZ_4K;
	dat = dpcm->phy_addr_rpc;
	vaddr = dpcm->vaddr_rpc;
	dma_free_coherent(dev, size, vaddr, dat);
fail_rpc:
	size = sizeof(struct snd_rtk_pcm);
	vaddr = dpcm;
	dat = dpcm->phy_addr;
	dma_free_coherent(dev, size, vaddr, dat);
	dpcm = NULL;
	runtime->private_data = NULL;
fail:
	return ret;
}

static int snd_card_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = substream->pcm->card->dev;
	struct snd_rtk_cap_pcm *dpcm = NULL;
	int ret = -ENOMEM;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	pr_info("[ALSA %s %s]\n", substream->pcm->name, __func__);

	size = sizeof(struct snd_rtk_cap_pcm);
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	dpcm = vaddr;
	memset(dpcm, 0, sizeof(struct snd_rtk_cap_pcm));
	dpcm->phy_addr = dat;

	/* Preparing a memory for all rpc */
	size = SZ_4K;
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		dev_err(dev, "%s dma_alloc fail\n", __func__);
		ret = -ENOMEM;
		goto fail_rpc;
	}

	dpcm->phy_addr_rpc = dat;
	dpcm->vaddr_rpc = vaddr;

	runtime->private_data = dpcm;
	runtime->private_free = snd_card_capture_runtime_free;
	dpcm->substream = substream;

	pr_info("ALSA: Device number is %d\n", substream->pcm->device);
	switch (substream->pcm->device) {
	case 0:
		/* it's for HDMI-RX */
		dpcm->source_in = ENUM_AIN_HDMIRX;
		pr_info("ALSA: For HDMI-RX.\n");
		break;
	case 1:
		/* it's for I2S */
		dpcm->source_in = ENUM_AIN_I2S;
		pr_info("ALSA: For I2S.\n");
		break;
	case 3:
		/* it's for aec mixing application */
		dpcm->source_in = ENUM_AIN_AEC_MIX;
		pr_info("ALSA: For aec mixing application.\n");
		break;
	case 4:
		/* it's for audio processing v2 DMIC LOOPBACK */
		dpcm->source_in = ENUM_AIN_AUDIO_V2;
		pr_info("ALSA: For audio processing v2 DMIC LOOPBACK.\n");
		break;
	case 5:
		/* it's for audio processing v3 SPEECH RECOGNITION FROM DMIC */
		dpcm->source_in = ENUM_AIN_AUDIO_V3;
		pr_info("ALSA: For audio processing v3 SPEECH RECOGNITION FROM DMIC.\n");
		break;
	case 6:
		/* it's for audio processing v4 SPEECH RECOGNITION FROM I2S */
		dpcm->source_in = ENUM_AIN_AUDIO_V4;
		pr_info("ALSA: For audio processing v4 SPEECH RECOGNITION FROM I2S.\n");
		break;
	case 7:
		/* it's for ao i2s loopback to ain */
		dpcm->source_in = ENUM_AIN_I2S_LOOPBACK;
		pr_info("ALSA: For ao i2s loopback to ain.\n");
		break;
	case 8:
		/* it's for dmic pass through to ao */
		dpcm->source_in = ENUM_AIN_DMIC_PASSTHROUGH;
		pr_info("ALSA: For dmic pass through to ao.\n");
		/* Create global ao id */
		rpc_create_global_ao(dpcm->phy_addr_rpc,
				     dpcm->vaddr_rpc, &dpcm->ao_agent_id);
		/* get ao flash pin ID */
		dpcm->ao_pin_id = rpc_get_ao_flash_pin(dpcm->phy_addr_rpc,
						       dpcm->vaddr_rpc,
						       dpcm->ao_agent_id);
		if (dpcm->ao_pin_id < 0) {
			pr_err("[can't get flash pin %s]\n", __func__);
			goto fail_1;
		}
		break;
	case 9:
		/* it's for pure DMIC */
		dpcm->source_in = ENUM_AIN_PURE_DMIC;
		pr_info("ALSA: This is for pure DMIC.\n");
		break;
	default:
		pr_info("ALSA: Not in the list maybe for non-pcm.\n");
		break;
	}

	memcpy(&runtime->hw, &rtk_snd_card_capture,
	       sizeof(struct snd_pcm_hardware));

	/* create or get AI */
	if (snd_realtek_hw_create_AI(substream) < 0) {
		pr_err("[error %s]\n", __func__);
		ret = -ENOMEM;
		goto fail_1;
	}

	dpcm->mixer = (struct rtk_snd_mixer *)(substream->pcm->card->private_data);

	/* init hr timer */
	hrtimer_init(&dpcm->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	spin_lock_init(&dpcm->capture_lock);

	ret = 0;

	return ret;
fail_1:
	size = SZ_4K;
	dat = dpcm->phy_addr_rpc;
	vaddr = dpcm->vaddr_rpc;
	dma_free_coherent(dev, size, vaddr, dat);
fail_rpc:
	size = sizeof(struct snd_rtk_cap_pcm);
	vaddr = dpcm;
	dat = dpcm->phy_addr;
	dma_free_coherent(dev, size, vaddr, dat);
	dpcm = NULL;
	runtime->private_data = NULL;
fail:
	return ret;
}

static int snd_card_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct device *dev = substream->pcm->card->dev;
	struct audio_rpc_sendio sendio;
	ktime_t remaining;
	int ret = -1;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	pr_info("[ALSA %s]\n", __func__);

	if (!dpcm)
		return -1;

	if (dpcm->source_in == ENUM_AIN_DMIC_PASSTHROUGH) {
		/* stop AO */
		rpc_stop_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
			     dpcm->ao_agent_id | dpcm->ao_pin_id);

		rpc_release_ao_flash_pin(dpcm->phy_addr_rpc,
					 dpcm->vaddr_rpc,
					 dpcm->ao_agent_id,
					 dpcm->ao_pin_id);
		/* AO flush */
		sendio.instance_id = dpcm->ao_agent_id;
		sendio.pin_id = dpcm->ao_pin_id;

		if (rpc_flush_svc(dpcm->phy_addr_rpc,
				  dpcm->vaddr_rpc, &sendio)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			goto exit;
		}
	}

	/* destroy AI including pause/stop/destroy */
	if (rpc_destroy_ai_flow_svc(dpcm->phy_addr_rpc,
				    dpcm->vaddr_rpc,
				    dpcm->ai_agent_id,
				    dpcm->init_ring)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	/* destroy the hr timer */
	remaining = hrtimer_get_remaining(&dpcm->hr_timer);
	if (ktime_to_ns(remaining) > 0)
		ndelay(ktime_to_ns(remaining));

	ret = hrtimer_cancel(&dpcm->hr_timer);
	if (ret) {
		pr_err("The timer still alive...\n");
		goto exit;
	}

	ret = 0;
exit:
	size = SZ_4K;
	dat = dpcm->phy_addr_rpc;
	vaddr = dpcm->vaddr_rpc;
	dma_free_coherent(dev, size, vaddr, dat);

	snd_realtek_hw_capture_free_ring(runtime);

	if (snd_open_ai_count > 0)
		snd_open_ai_count--;

	return ret;
}

static int snd_card_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;

	if (dpcm->init_ring)
		rtk_playback_realtime_function(runtime);
	return 0;
}

static int snd_card_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct device *dev = substream->pcm->card->dev;
	struct audio_rpc_sendio sendio;
	int ret = 0;
	phys_addr_t dat;
	void *vaddr;
	size_t size;

	pr_info("[ALSA %s %d]\n", __func__, __LINE__);

	rpc_put_share_memory_latency(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				     NULL, NULL, 0, dpcm->ao_pin_id,
				     ENUM_PRIVATEINFO_AUDIO_GET_SHARE_MEMORY_FROM_ALSA);

	if (dpcm->ao_pin_id) {
		/* stop decoder */
		if (dpcm->dec_agent_id && dpcm->ao_decode_lpcm)
			rpc_stop_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				     dpcm->dec_agent_id);

		/* AO pause */
		if (rpc_pause_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
				  dpcm->ao_agent_id | dpcm->ao_pin_id)) {
			ret = -1;
			goto exit;
		}

		if (dpcm->init_ring != 0) {
			if (dpcm->ao_decode_lpcm) {
				/* decoder flush */
				sendio.instance_id = dpcm->dec_agent_id;
				sendio.pin_id = dpcm->dec_pin_id;

				if (rpc_flush_svc(dpcm->phy_addr_rpc,
						  dpcm->vaddr_rpc, &sendio)) {
					ret = -1;
					goto exit;
				}
			}

			/* stop AO */
			rpc_stop_svc(dpcm->phy_addr_rpc,
				     dpcm->vaddr_rpc, dpcm->ao_agent_id | dpcm->ao_pin_id);
		}

		/* destroy decoder instance if exist */
		if (dpcm->dec_agent_id && dpcm->ao_decode_lpcm)
			rpc_destroy_svc(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
					dpcm->dec_agent_id);
	} else {
		return -1;
	}

#ifdef DEBUG_RECORD
	/* Close the record file */
	if (dpcm->pos != -1)
		filp_close(dpcm->fp, NULL);
#endif

exit:
	rpc_release_ao_flash_pin(dpcm->phy_addr_rpc,
				 dpcm->vaddr_rpc,
				 dpcm->ao_agent_id,
				 dpcm->ao_pin_id);
	snd_open_count--;

	size = SZ_4K;
	dat = dpcm->phy_addr_rpc;
	vaddr = dpcm->vaddr_rpc;
	dma_free_coherent(dev, size, vaddr, dat);

	snd_realtek_hw_free_ring(runtime);

	if (dpcm->g_sharemem_ptr) {
		size = SZ_4K;
		dat = dpcm->g_sharemem_ptr_dat;
		vaddr = dpcm->g_sharemem_ptr;
		dma_free_coherent(dev, size, vaddr, dat);
	}

	if (dpcm->g_sharemem_ptr2) {
		size = sizeof(struct alsa_latency_info);
		dat = dpcm->g_sharemem_ptr_dat2;
		vaddr = dpcm->g_sharemem_ptr2;
		dma_free_coherent(dev, size, vaddr, dat);
	}

	if (dpcm->g_sharemem_ptr3) {
		size = sizeof(struct alsa_latency_info);
		dat = dpcm->g_sharemem_ptr_dat3;
		vaddr = dpcm->g_sharemem_ptr3;
		dma_free_coherent(dev, size, vaddr, dat);
	}

	return ret;
}

static unsigned int snd_capture_monitor_delay(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	unsigned int sample_rate = runtime->rate;
	unsigned int ret = 0;
	unsigned long base, limit, rp, wp;
	unsigned int pcm_size;      /* unit: sample */
	unsigned int pcm_latency;   /* unit: ms */
	unsigned int lpcm_size;     /* unit: sample */
	unsigned int lpcm_latency;  /* unit: ms */

	/* calculate the size of PCM (input of AI) */
	base = (unsigned long)(dpcm->airing[0]->begin_addr);
	limit = (unsigned long)(dpcm->airing[0]->begin_addr + dpcm->airing[0]->size);
	wp = (unsigned long)dpcm->airing[0]->write_ptr;
	rp = (unsigned long)dpcm->airing[0]->read_ptr[0];
	pcm_size = ring_valid_data(base, limit, rp, wp) >> 2;

	/* calculate the size of LPCM (output of AI) */
	base = (unsigned long)dpcm->lpcm_ring->begin_addr;
	limit = (unsigned long)(dpcm->lpcm_ring->begin_addr + dpcm->lpcm_ring->size);
	wp = (unsigned long)dpcm->lpcm_ring->write_ptr;
	rp = (unsigned long)dpcm->lpcm_ring->read_ptr[0];
	lpcm_size = ring_valid_data(base, limit, rp, wp);

	switch (dpcm->ai_format) {
	case AUDIO_ALSA_FORMAT_16BITS_LE_LPCM:
		/* 2ch, 2bytes per sample */
		lpcm_size >>= 2;
		break;
	case AUDIO_ALSA_FORMAT_24BITS_LE_LPCM:
		/* 2ch, 3bytes per sample */
		lpcm_size /= 6;
		break;
	case AUDIO_ALSA_FORMAT_32BITS_LE_LPCM:
		/* 2ch, 4bytes per sample */
		lpcm_size /= 8;
		break;
	default:
		pr_err("capture err, %d @ %s\n", dpcm->ai_format, __func__);
	}

	/* calculate leatency */
	pcm_latency = (pcm_size * 1000) / sample_rate;
	lpcm_latency = (lpcm_size * 1000) / sample_rate;
	ret = pcm_latency + lpcm_latency;

	return ret;
}

static snd_pcm_uframes_t snd_card_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t ret = 0;

	/* update hw_ptr */
	ret = dpcm->total_write % runtime->buffer_size;
	return ret;
}

static snd_pcm_uframes_t snd_card_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t ret = 0;
	snd_pcm_uframes_t read_addsize = 0;
	unsigned int hw_ringrp;

	/* read HW RP (the pointer of FW read) */
	hw_ringrp = (unsigned int)dpcm->dec_inring->read_ptr[0];
	dpcm->hw_ptr = bytes_to_frames(runtime,
				       (unsigned long)hw_ringrp -
				       (unsigned long)dpcm->dec_inring->begin_addr);

	/* update HW read size */
	if (dpcm->hw_ptr != dpcm->prehw_ptr) {
		read_addsize = ring_valid_data(0, runtime->buffer_size,
					       dpcm->prehw_ptr, dpcm->hw_ptr);

		dpcm->total_read = ring_add(0, runtime->boundary,
					    dpcm->total_read,
					    read_addsize);
	}

#ifdef DEBUG_RECORD
	if (dpcm->pos != -1) {
		dpcm->fs = force_uaccess_begin();
		if (dpcm->hw_ptr >= dpcm->prehw_ptr) {
			kernel_write(dpcm->fp,
				  runtime->dma_area +
				  frames_to_bytes(runtime, dpcm->prehw_ptr),
				  frames_to_bytes(runtime, dpcm->hw_ptr - dpcm->prehw_ptr),
				  &dpcm->pos);
		} else {
			read_addsize = ring_valid_data(0, runtime->buffer_size,
						       dpcm->prehw_ptr,
						       dpcm->hw_ptr);

			kernel_write(dpcm->fp,
				  runtime->dma_area +
				  frames_to_bytes(runtime, dpcm->prehw_ptr),
				  frames_to_bytes(runtime, runtime->buffer_size - dpcm->prehw_ptr),
				  &dpcm->pos);
			kernel_write(dpcm->fp, runtime->dma_area,
				  frames_to_bytes(runtime, read_addsize -
				  (runtime->buffer_size - dpcm->prehw_ptr)),
				  &dpcm->pos);
		}
		force_uaccess_end(dpcm->fs);
	}
#endif

	/* Clear the buffer after reading it */
	if (dpcm->hw_ptr >= dpcm->prehw_ptr) {
		memset(runtime->dma_area + frames_to_bytes(runtime, dpcm->prehw_ptr),
		       0x0,
		       frames_to_bytes(runtime, dpcm->hw_ptr - dpcm->prehw_ptr));
	} else {
		memset(runtime->dma_area + frames_to_bytes(runtime, dpcm->prehw_ptr),
		       0x0,
		       frames_to_bytes(runtime, runtime->buffer_size - dpcm->prehw_ptr));
		memset(runtime->dma_area, 0x0,
		       frames_to_bytes(runtime, read_addsize -
				      (runtime->buffer_size - dpcm->prehw_ptr)));
	}

	/* check if RP of FW stop */
	if (dpcm->hw_ptr == dpcm->prehw_ptr)
		dpcm->dbg_count = dpcm->dbg_count + 1;
	else
		dpcm->dbg_count = 0;

	if (dpcm->dbg_count >= HZ) {
		pr_err("[state %d]\n", runtime->status->state);
		pr_err("[runtime->control->appl_ptr %d runtime->status->hw_ptr %d]\n",
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr);
		pr_err("[dpcm->total_write %d dpcm->total_read %d dpcm->hw_ptr %d]\n",
		       (int)dpcm->total_write,
		       (int)dpcm->total_read,
		       (int)dpcm->hw_ptr);
		pr_err("[b %x l %x w %x r %x]\n",
		       (unsigned int)dpcm->dec_inring->begin_addr,
		       (unsigned int)(dpcm->dec_inring->begin_addr + dpcm->dec_inring->size),
		       (unsigned int)dpcm->dec_inring->write_ptr,
		       (unsigned int)dpcm->dec_inring->read_ptr[0]);
		pr_err("[hw_ringrp %x runtime->period_size %d runtime->periods %d]\n",
		       hw_ringrp, runtime->period_size, runtime->periods);
		pr_err("[snd_pcm_playback_avail %d runtime->control->avail_min %d]\n",
		       (int)snd_pcm_playback_avail(runtime),
		       (int)runtime->control->avail_min);
		dpcm->dbg_count = 0;
	}

	dpcm->prehw_ptr = dpcm->hw_ptr;

	/* update runtime->status->hw_ptr */
	ret = dpcm->total_read % runtime->buffer_size;
	return ret;
}

static int snd_card_capture_prepare_LPCM(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;

	/* decide ai ring buf or not */
	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
		break;
	default:
		/* malloc AI ring buf */
		if (rtk_snd_capture_malloc_ring(runtime)) {
			pr_err("[%s fail]\n", __func__);
			return -ENOMEM;
		}

		/* init ring header of AI */
		if (rtk_snd_init_ringheader_AI(runtime)) {
			pr_err("[%s fail]\n", __func__);
			return -ENOMEM;
		}
		break;
	}

	/* decide pts buf or not */
	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
	case ENUM_AIN_DMIC_PASSTHROUGH:
		break;
	default:
		/* malloc PTS ring buf */
		if (rtk_snd_capture_malloc_pts_ring(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		/* init PTS ring header of AI */
		if (rtk_snd_capture_PTS_ringheader_AI(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		break;
	}

	/* malloc LPCM ring buf */
	if (rtk_snd_capture_malloc_lpcm_ring(runtime)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -ENOMEM;
	}

	/* init LPCM ring header of AI */
	if (rtk_snd_capture_LPCM_ringheader_AI(runtime)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -ENOMEM;
	}

	/* config for different protocol */
	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
		/* For audio processing v2 or v3 flow */
		if (rpc_ai_connect_alsa(dpcm->phy_addr_rpc,
					dpcm->vaddr_rpc, runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		/* config audio v2 or v3 */
		rpc_ai_config_audio_in(dpcm->phy_addr_rpc,
				       dpcm->vaddr_rpc, dpcm);
		break;
	case ENUM_AIN_HDMIRX:
		/* config HDMI-RX */
		rpc_ai_config_hdmi_rx_in(dpcm->phy_addr_rpc,
					 dpcm->vaddr_rpc, dpcm);
		break;
	case ENUM_AIN_I2S:
		/* config I2S */
		rpc_ai_config_i2s_in(dpcm->phy_addr_rpc,
				     dpcm->vaddr_rpc, dpcm);
		break;
	case ENUM_AIN_I2S_LOOPBACK:
		/* config I2S loopback */
		rpc_ai_config_i2s_loopback_in(dpcm->phy_addr_rpc,
					      dpcm->vaddr_rpc, dpcm);
		break;
	case ENUM_AIN_DMIC_PASSTHROUGH:
		/* init ringheader of AO_using ai ring */
		if (rtk_snd_init_AO_ringheader_by_AI(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		/* connect AI and AO */
		if (rtk_snd_init_connect(dpcm->phy_addr_rpc,
					 dpcm->vaddr_rpc, dpcm->ai_agent_id,
					 dpcm->ao_agent_id, dpcm->ao_pin_id)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		/* It must be called when AI pass through */
		if (rpc_ai_connect_ao(dpcm->phy_addr_rpc,
				      dpcm->vaddr_rpc, dpcm)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		/* config dmic passthrough */
		rpc_ai_config_dmic_in(dpcm->phy_addr_rpc, dpcm->vaddr_rpc, dpcm);
		break;
	case ENUM_AIN_PURE_DMIC:
		/* config pure dmic */
		rpc_ai_config_dmic_in(dpcm->phy_addr_rpc, dpcm->vaddr_rpc, dpcm);
		break;
	default:
		break;
	}

	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
		break;
	default:
		if (rpc_ai_connect_alsa(dpcm->phy_addr_rpc,
					dpcm->vaddr_rpc, runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		break;
	}

	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
		break;
	default:
		if (snd_realtek_hw_capture_run(dpcm)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
		break;
	}

	dpcm->hr_timer.function = &rtk_capture_timer_function;

	return 0;
}

static int snd_card_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;

	if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
		pr_err("[SNDRV_PCM_STATE_XRUN appl_ptr %d hw_ptr %d]\n",
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr);
	}

	/* Setup the hr timer using ktime */
	dpcm->ktime = ktime_set(0, (runtime->period_size * 1000) / runtime->rate * 1000 * 1000);

	pr_info("\n");
	pr_info("Capture:");
	pr_info("rate %d channels %d format %x\n",
		runtime->rate,
		runtime->channels,
		runtime->format);
	pr_info("period_size %d periods %d\n",
		(int)runtime->period_size,
		(int)runtime->periods);
	pr_info("buffer_size %d\n", (int)runtime->buffer_size);
	pr_info("start_threshold %d stop_threshold %d\n",
		(int)runtime->start_threshold,
		(int)runtime->stop_threshold);
	pr_info("[runtime->frame_bits %d]\n", runtime->frame_bits);
	pr_info("[runtime->sample_bits %d]\n", runtime->sample_bits);
	pr_info("\n");

	switch (runtime->access) {
	case SNDRV_PCM_ACCESS_MMAP_INTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_INTERLEAVED:
		switch (runtime->format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			pr_info("[SNDRV_PCM_FORMAT_S16_LE]\n");
			dpcm->ai_format = AUDIO_ALSA_FORMAT_16BITS_LE_LPCM;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			pr_info("[SNDRV_PCM_FORMAT_S24_LE]\n");
			dpcm->ai_format = AUDIO_ALSA_FORMAT_24BITS_LE_LPCM;
			break;
		case SNDRV_PCM_FORMAT_S24_3LE:
			pr_info("[SNDRV_PCM_FORMAT_S24_3LE]\n");
			dpcm->ai_format = AUDIO_ALSA_FORMAT_24BITS_LE_LPCM;
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			pr_info("[SNDRV_PCM_FORMAT_S32_LE]\n");
			dpcm->ai_format = AUDIO_ALSA_FORMAT_32BITS_LE_LPCM;
			break;
		default:
			pr_info("[unsupport format %d %s]\n", runtime->format
				, __func__);
			return -1;
		}
		break;
	case SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_NONINTERLEAVED:
	default:
		pr_err("[unsupport access @ %s %d]\n", __func__, __LINE__);
		return -1;
	}

	if (dpcm->init_ring) {
		dpcm->total_write = 0;
		pr_err("[Re-Prepare %d %d %s]\n",
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr,
		       __func__);
		return 0;
	}

	dpcm->period_bytes = frames_to_bytes(runtime, runtime->period_size);
	dpcm->frame_bytes = frames_to_bytes(runtime, 1);

	switch (dpcm->ai_format) {
	case AUDIO_ALSA_FORMAT_16BITS_LE_LPCM:
	case AUDIO_ALSA_FORMAT_24BITS_LE_LPCM:
	case AUDIO_ALSA_FORMAT_32BITS_LE_LPCM:
		if (snd_card_capture_prepare_LPCM(substream)) {
			pr_err("[%s fail]\n", __func__);
			return -ENOMEM;
		}
		break;
	default:
		pr_err("[ALSA err %s]\n", __func__);
		break;
	}

	dpcm->init_ring = 1;

	return 0;
}

static int snd_card_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;

	if (is_suspend) {
		pr_err("[ALSA %s] suspend\n", __func__);
		return 0;
	}

	if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
		pr_err("[SNDRV_PCM_STATE_XRUN appl_ptr %d hw_ptr %d]\n",
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr);
	}

	if (dpcm->init_ring) {
		/* Reset the ptr about playback.
		 * prehw_ptr will influence the next ptr update in timer function.
		 */
		dpcm->total_read = 0;
		dpcm->total_write = 0;
		dpcm->prehw_ptr = 0;

		/* destroy the decoder for re-sending NewFormat */
		snd_realtek_reprepare(runtime);
		pr_info("[Re-Prepare %d %d %s]\n",
			(int)runtime->control->appl_ptr,
			(int)runtime->status->hw_ptr,
			__func__);
	}

	pr_info("[======START======]\n");
	pr_info("[playback : rate %d channels %d]\n",
		runtime->rate,
		runtime->channels);
	pr_info("[period_size %d periods %d]\n",
		(int)runtime->period_size,
		(int)runtime->periods);
	pr_info("[buffer_size %d]\n",
		(int)runtime->buffer_size);
	pr_info("[start_threshold %d stop_threshold %d]\n",
		(int)runtime->start_threshold,
		(int)runtime->stop_threshold);
	pr_info("[runtime->access %d]\n", runtime->access);
	pr_info("[runtime->format %d]\n", runtime->format);
	pr_info("[runtime->frame_bits %d]\n", runtime->frame_bits);
	pr_info("[runtime->sample_bits %d]\n", runtime->sample_bits);
	pr_info("[runtime->silence_threshold %d]\n",
		(int)runtime->silence_threshold);
	pr_info("[runtime->silence_size %d]\n", (int)runtime->silence_size);
	pr_info("[runtime->boundary %d]\n", (int)runtime->boundary);
	pr_info("[runtime->min_align %d]\n", (int)runtime->min_align);
	pr_info("[runtime->hw_ptr_base 0x%x]\n",
		(unsigned int)(uintptr_t)runtime->hw_ptr_base);
	pr_info("[runtime->dma_area 0x%x]\n",
		(unsigned int)(uintptr_t)runtime->dma_area);
	pr_info("[======END======]\n");

	dpcm->period_bytes = frames_to_bytes(runtime, runtime->period_size);
	dpcm->last_channel = runtime->channels;

	if (dpcm->ao_decode_lpcm) {
		/* create decoder agent */
		if (rpc_create_decoder_agent(dpcm->phy_addr_rpc,
					     dpcm->vaddr_rpc,
					     &dpcm->dec_agent_id,
					     &dpcm->dec_pin_id)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		rpc_put_share_memory_latency(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
					     (void *)dpcm->g_sharemem_ptr_dat,
					     (void *)dpcm->g_sharemem_ptr_dat2,
					     dpcm->dec_agent_id, dpcm->ao_pin_id,
					     ENUM_PRIVATEINFO_AUDIO_GET_SHARE_MEMORY_FROM_ALSA);

		/* malloc ao_inring */
		if (rtk_snd_malloc_ring(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		/* init ringheader of decoder_outring and AO_inring */
		if (rtk_snd_init_ringheader_DEC_AO(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		/* connect decoder and AO by RPC */
		if (rtk_snd_init_connect(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
					 dpcm->dec_agent_id, dpcm->ao_agent_id, dpcm->ao_pin_id)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		/* init decoder in_ring and decoder inband ring */
		if (rtk_snd_init_decoder_inring(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}
	} else {
		rpc_put_share_memory_latency(dpcm->phy_addr_rpc, dpcm->vaddr_rpc,
					     (void *)dpcm->g_sharemem_ptr_dat,
					     (void *)dpcm->g_sharemem_ptr_dat2,
					     -1, dpcm->ao_pin_id,
					     ENUM_PRIVATEINFO_AUDIO_GET_SHARE_MEMORY_FROM_ALSA);

		/* init ringheader of AO_inring without decoder */
		if (rtk_snd_init_ringheader_AO(runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -ENOMEM;
		}

		if (rpc_ao_config_without_decoder(dpcm->phy_addr_rpc,
						  dpcm->vaddr_rpc, runtime)) {
			pr_err("[%s %d fail]\n", __func__, __LINE__);
			return -1;
		}
	}

	if (rpc_pause_svc(dpcm->phy_addr_rpc,
			  dpcm->vaddr_rpc,
			  dpcm->ao_agent_id | dpcm->ao_pin_id)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		return -1;
	}

	if (dpcm->ao_decode_lpcm) {
		if (rtk_snd_init_decoder_info(runtime)) {
			pr_err("[%s %d]\n", __func__, __LINE__);
			return -ENOMEM;
		}
	}

	if (snd_realtek_hw_resume(dpcm)) {
		pr_err("[%s %d]\n", __func__, __LINE__);
		return -ENOMEM;
	}

	dpcm->init_ring = 1;

	return 0;
}

static long snd_card_get_ring_data(struct ringbuffer_header *ring)
{
	unsigned long base, limit, rp, wp, data_size;

	base = (unsigned long)ring->begin_addr;
	limit = (unsigned long)(ring->begin_addr + ring->size);
	wp = (unsigned long)ring->write_ptr;
	rp = (unsigned long)ring->read_ptr[0];

	data_size = ring_valid_data(base, limit, rp, wp);

	return data_size;
}

static long ring_memcpy2_buf(char *buf,
			     unsigned long base,
			     unsigned long limit,
			     unsigned long ptr,
			     unsigned int size)
{
	if (ptr + size <= limit) {
		memcpy(buf, (char *)ptr, size);
	} else {
		int i = limit - ptr;

		memcpy((char *)buf, (char *)ptr, i);
		memcpy((char *)(buf + i), (char *)base, size - i);
	}

	ptr += size;
	if (ptr >= limit)
		ptr = base + (ptr - limit);

	return ptr;
}

u64 rtk_get_90k_pts_hifi(void)
{
	return refclk_get_val_raw();
}

static void snd_card_capture_setup_pts(struct snd_pcm_runtime *runtime,
				       struct audio_dec_pts_info *pkt)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	u64 cur_pts, pcm_pts, diff_pts;
	unsigned int pcm_pts_hi, pcm_pts_lo;

	/* get current system PTS */
	cur_pts = rtk_get_90k_pts_hifi();

	pcm_pts_hi = pkt->PTSH;
	pcm_pts_lo = pkt->PTSL;
	pcm_pts = (((u64)pcm_pts_hi) << 32) | ((u64)pcm_pts_lo);

	/* the PTS offset between kerenl and fw */
	diff_pts = cur_pts - pcm_pts;

	/* 1 second = 90000PTS */
	ktime_get_ts64(&dpcm->ts);
	dpcm->ts.tv_sec -= (div64_ul(diff_pts, 90000));
	dpcm->ts.tv_nsec -= (div64_ul(diff_pts * 100000, 9));

	/* If nsec smaller than zero, it means one sec before */
	if (dpcm->ts.tv_nsec < 0) {
		dpcm->ts.tv_nsec += 1000000000;
		dpcm->ts.tv_sec--;
	}
}

static void snd_card_capture_calculate_pts(struct snd_pcm_runtime *runtime,
					   long period_count)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct audio_dec_pts_info pkt[2];
	struct audio_ringbuf_ptr_64 *p_ring = &dpcm->pts_ring;
	unsigned long temp_rp;
	unsigned int lpcm_rp = dpcm->lpcm_ring->read_ptr[0];
	unsigned int npkt_ptr[2] = {0};
	unsigned int wp_offset, rp_offset, loop_count = 0;

	/* refresh wp of pts_ring */
	wp_offset = dpcm->pts_ring_hdr->write_ptr - (unsigned int)dpcm->pts_mem.p_phy;
	p_ring->wp = p_ring->base + (unsigned long)wp_offset;

	/* get the first PTS */
	if (ring_valid_data(p_ring->base, p_ring->limit, p_ring->rp, p_ring->wp)
					>= sizeof(struct audio_dec_pts_info)) {
		temp_rp = ring_memcpy2_buf((char *)&pkt[0],
					   p_ring->base,
					   p_ring->limit,
					   p_ring->rp,
					   sizeof(struct audio_dec_pts_info));
	} else {
		pr_err("Err %s\n", __func__);
		goto exit;
	}

	do {
		if (ring_valid_data(p_ring->base, p_ring->limit, temp_rp, p_ring->wp)
						>= sizeof(struct audio_dec_pts_info)) {
			ring_memcpy2_buf((char *)&pkt[1],
					 p_ring->base,
					 p_ring->limit,
					 temp_rp,
					 sizeof(struct audio_dec_pts_info));
		} else {
			if (loop_count == 0) {
				snd_card_capture_setup_pts(runtime, &pkt[0]);
				goto exit;
			} else {
				/* get the last packet as PTS needed */
				snd_card_capture_setup_pts(runtime, &pkt[0]);
				p_ring->rp = ring_minus(p_ring->base,
							p_ring->limit,
							temp_rp,
							sizeof(struct audio_dec_pts_info));
				break;
			}
		}

		npkt_ptr[0] = pkt[0].w_ptr;
		npkt_ptr[1] = pkt[1].w_ptr;

		if (ring_check_ptr_valid_32(npkt_ptr[0], npkt_ptr[1], lpcm_rp)) {
			/* get PTS */
			snd_card_capture_setup_pts(runtime, &pkt[0]);
			p_ring->rp = ring_minus(p_ring->base,
						p_ring->limit,
						temp_rp,
						sizeof(struct audio_dec_pts_info));
			break;
		}

		temp_rp = ring_add(p_ring->base,
				   p_ring->limit,
				   temp_rp,
				   sizeof(struct audio_dec_pts_info));

		pkt[0] = pkt[1];
		loop_count++;
	} while (1);

	/* update rp */
	rp_offset = p_ring->rp - p_ring->base;
	dpcm->pts_ring_hdr->read_ptr[0] = (unsigned int)dpcm->pts_mem.p_phy + rp_offset;

exit:
	return;
}

static void snd_card_capture_LPCM_copy(struct snd_pcm_runtime *runtime, long period_count)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t frame_size = period_count * runtime->period_size;
	snd_pcm_uframes_t dma_wp = dpcm->total_write % runtime->buffer_size;
	struct audio_ringbuf_ptr_64 src_ring, dst_ring;

	src_ring.base = (unsigned long)dpcm->lpcm_data;
	src_ring.limit = (unsigned long)(src_ring.base + dpcm->lpcm_ring->size);
	src_ring.rp = src_ring.base
		+ (unsigned long)(dpcm->lpcm_ring->read_ptr[0] - dpcm->lpcm_ring->begin_addr);

	dst_ring.base = (unsigned long)runtime->dma_area;
	dst_ring.limit = (unsigned long)(runtime->dma_area +
					 runtime->buffer_size * dpcm->frame_bytes);
	dst_ring.wp = (unsigned long)(runtime->dma_area + dma_wp * dpcm->frame_bytes);

	ring1_to_ring2_general_64(&src_ring, &dst_ring, frame_size * dpcm->frame_bytes);
}

int snd_card_capture_get_time_info(struct snd_pcm_substream *substream,
				   struct timespec64 *system_ts, struct timespec64 *audio_ts,
				   struct snd_pcm_audio_tstamp_config *audio_tstamp_config,
				   struct snd_pcm_audio_tstamp_report *audio_tstamp_report)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	u64 audio_frames, audio_nsecs;

	*system_ts = dpcm->ts;

	audio_frames = runtime->hw_ptr_wrap + runtime->status->hw_ptr;
	audio_nsecs = div_u64(audio_frames * 1000000000LL,
			      runtime->rate);
	*audio_ts = ns_to_timespec64(audio_nsecs);

	return 0;
}

static void rtk_snd_capture_handle_HDMI_plug_out(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	unsigned int period_count = 1;
	snd_pcm_uframes_t frame_size = period_count * runtime->period_size;
	snd_pcm_uframes_t dma_wp = dpcm->total_write % runtime->buffer_size;
	struct audio_ringbuf_ptr_64 dst_ring;
	char *pbuf = kmalloc(frame_size * dpcm->frame_bytes, GFP_KERNEL);
	unsigned long free_size;

	if (!pbuf) {
		pr_err("malloc FAILED @ %s %d\n", __func__, __LINE__);
		return;
	}

	free_size = snd_pcm_capture_hw_avail(runtime);
	if (free_size <= runtime->period_size) {
		pr_err("over flow %d %d @ %s\n",
		       (int)free_size,
		       (int)runtime->period_size, __func__);
		kfree(pbuf);
		return;
	}

	pr_err(" @ %s\n", __func__);

	/* copy MUTE to DMA buffer */
	memset(pbuf, 0, frame_size * dpcm->frame_bytes);
	dst_ring.base = (unsigned long)runtime->dma_area;
	dst_ring.limit = (unsigned long)(runtime->dma_area +
					 runtime->buffer_size * dpcm->frame_bytes);
	dst_ring.wp = (unsigned long)(runtime->dma_area + dma_wp * dpcm->frame_bytes);
	buf_memcpy2_ring(dst_ring.base, dst_ring.limit,
			 dst_ring.wp, pbuf,
			 frame_size * dpcm->frame_bytes);
	kfree(pbuf);

	/* paste time stamp */
	ktime_get_ts64(&dpcm->ts);

	dpcm->total_write += period_count * runtime->period_size;
	snd_pcm_period_elapsed(substream);
}

static enum hrtimer_restart rtk_capture_timer_function(struct hrtimer *timer)
{
	struct snd_rtk_cap_pcm *dpcm =
		container_of(timer, struct snd_rtk_cap_pcm, hr_timer);
	struct snd_pcm_substream *substream = dpcm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t ring_data_frame, free_size;
	long ring_data_size;
	unsigned int period_count = 0, free_period;

	if (dpcm->en_hr_timer == HRTIMER_RESTART) {
		if (ring_valid_data(0,
				    (unsigned long)runtime->boundary,
				    (unsigned long)runtime->control->appl_ptr,
				    (unsigned long)runtime->status->hw_ptr)
				     > runtime->buffer_size) {
			pr_err("[hw_ptr %d appl_ptr %d %d @ %s %d]\n",
			       (int)runtime->status->hw_ptr,
			       (int)runtime->control->appl_ptr,
			       (int)runtime->buffer_size, __func__, __LINE__);
		}

		/* check if HDMI-RX plug out */
		if (dpcm->source_in == ENUM_AIN_HDMIRX) {
			if (rtk_snd_capture_hdmirx_enable() == 0) {
				rtk_snd_capture_handle_HDMI_plug_out(substream);
				goto SET_TIMER;
			}
		}

		ring_data_size = snd_card_get_ring_data(dpcm->lpcm_ring);
		ring_data_frame = ring_data_size / dpcm->frame_bytes;
		if (ring_data_frame >= runtime->period_size) {
			period_count = ring_data_frame / runtime->period_size;

			if (period_count == runtime->periods)
				period_count--;

			/* check overflow */
			{
				free_size = runtime->buffer_size - ring_valid_data(0
					, (unsigned long)runtime->boundary
					, (unsigned long)runtime->control->appl_ptr
					, (unsigned long)runtime->status->hw_ptr);
				free_period = free_size / runtime->period_size;
				period_count = min(period_count, free_period);
				if (period_count == 0)
					goto SET_TIMER;
			}

			/* copy data from LPCM_ring to dma_buf */
			snd_card_capture_LPCM_copy(runtime, period_count);

			/* calculate PTS */
			switch (dpcm->source_in) {
			case ENUM_AIN_AUDIO_V2:
			case ENUM_AIN_AUDIO_V3:
			case ENUM_AIN_AUDIO_V4:
			case ENUM_AIN_DMIC_PASSTHROUGH:
				break;
			default:
				snd_card_capture_calculate_pts(runtime, period_count);
				break;
			}

			/* update LPCM_ring rp */
			dpcm->lpcm_ring->read_ptr[0] =
				ring_add(dpcm->lpcm_ring->begin_addr,
					 (dpcm->lpcm_ring->begin_addr + dpcm->lpcm_ring->size),
					 (dpcm->lpcm_ring->read_ptr[0]),
					 period_count * runtime->period_size * dpcm->frame_bytes);

			dpcm->total_write += period_count * runtime->period_size;

			/* update runtime->status->hw_ptr */
			snd_pcm_period_elapsed(substream);
		}

SET_TIMER:
		/* Set up the next time */
		hrtimer_forward_now(timer, dpcm->ktime);

		return HRTIMER_RESTART;
	} else {
		return HRTIMER_NORESTART;
	}
}

static void rtk_playback_realtime_function(struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	unsigned int period_count = 0;
	unsigned int hw_ring_free_size;
	unsigned int hw_ring_free_frame;
	unsigned int hw_ringrp;
	int dec_out_valid_size = 0;

	/* physical address */
	hw_ringrp = (unsigned int)dpcm->dec_inring->read_ptr[0];

	if (dpcm->ao_decode_lpcm) {
		/* calculate AOInring msec */
		dec_out_valid_size =
			ring_valid_data((unsigned int)dpcm->dec_out_ring[0]->begin_addr,
					(unsigned int)dpcm->dec_out_ring[0]->begin_addr +
					rtk_dec_ao_buffer,
					(unsigned int)dpcm->dec_out_ring[0]->read_ptr[0],
					(unsigned int)dpcm->dec_out_ring[0]->write_ptr);

		if (dec_out_valid_size > 0 && dec_out_valid_size <= dpcm->ring_size)
			dpcm->dec_out_msec = ((dec_out_valid_size >> 2) * 1000) / runtime->rate;
		else
			dpcm->dec_out_msec = 0;
	}

	/* update wp (the pointer application send data to alsa) */
	period_count = ring_valid_data(0, runtime->boundary,
				       dpcm->total_write,
				       runtime->control->appl_ptr) / runtime->period_size;

	/* Check the buffer available size between alsa and FW */
	hw_ring_free_size =
		valid_free_size(dpcm->dec_inring->begin_addr,
				dpcm->dec_inring->begin_addr + dpcm->dec_inring->size,
				hw_ringrp,
				dpcm->dec_inring->write_ptr);

	hw_ring_free_frame = bytes_to_frames(runtime, hw_ring_free_size);

	if ((runtime->period_size * period_count) > hw_ring_free_frame)
		period_count = hw_ring_free_frame / runtime->period_size;

	if (hw_ring_free_size <= dpcm->period_bytes)
		period_count = 0;

	if (period_count) {
		dpcm->dec_inring->write_ptr =
			ring_add(dpcm->dec_inring->begin_addr,
				 dpcm->dec_inring->begin_addr + dpcm->dec_inring->size,
				 dpcm->dec_inring->write_ptr,
				 frames_to_bytes(runtime, runtime->period_size * period_count));

		dpcm->total_write =
			ring_add(0, runtime->boundary,
				 dpcm->total_write,
				 runtime->period_size * period_count);
	}
}

static int snd_card_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	int ret = 0;

	/* error checking */
	if (snd_pcm_capture_avail(runtime) > runtime->buffer_size ||
	    snd_pcm_capture_hw_avail(runtime) > runtime->buffer_size) {
		pr_err("[ERROR BUG %d %s %s]\n", cmd, __FILE__, __func__);
		pr_err("[state %d appl_ptr %d hw_ptr %d %d]\n",
		       runtime->status->state,
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr,
		       (int)runtime->buffer_size);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dpcm->en_hr_timer = HRTIMER_RESTART;
		hrtimer_start(&dpcm->hr_timer, dpcm->ktime, HRTIMER_MODE_REL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dpcm->en_hr_timer = HRTIMER_NORESTART;
		hrtimer_try_to_cancel(&dpcm->hr_timer);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int snd_card_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	int ret = 0;

	if (cmd != SNDRV_PCM_TRIGGER_SUSPEND && is_suspend)
		return 0;

	/* error checking */
	if (snd_pcm_playback_avail(runtime) > runtime->buffer_size ||
	    snd_pcm_playback_hw_avail(runtime) > runtime->buffer_size) {
		pr_err("[ERROR BUG %s %s]\n", __FILE__, __func__);
		pr_err("[state %d appl_ptr %d hw_ptr %d]\n",
		       runtime->status->state,
		       (int)runtime->control->appl_ptr,
		       (int)runtime->status->hw_ptr);
		pr_err("[dpcm->total_write %d dpcm->total_read %d]\n",
		       (int)dpcm->total_write,
		       (int)dpcm->total_read);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
		break;
	case SNDRV_PCM_TRIGGER_START:
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		break;
	default:
		pr_err("[err unknown cmd %d %s]\n", cmd, __func__);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int snd_card_playback_mmap(struct snd_pcm_substream *substream,
				  struct vm_area_struct *area)
{
	struct snd_dma_buffer *dmab = substream->runtime->dma_buffer_p;
	struct device *dev = dmab->dev.dev;
	dma_addr_t daddr = dmab->addr;
	void *vaddr = dmab->area;
	size_t size = area->vm_end - area->vm_start;

	if (vaddr)
		return dma_mmap_coherent(dev, area, vaddr, daddr, size);

	return 0;
}

static int snd_card_capture_mmap(struct snd_pcm_substream *substream,
				 struct vm_area_struct *area)
{
	struct snd_dma_buffer *dmab = substream->runtime->dma_buffer_p;
	struct device *dev = dmab->dev.dev;
	dma_addr_t daddr = dmab->addr;
	void *vaddr = dmab->area;
	size_t size = area->vm_end - area->vm_start;

	if (vaddr)
		return dma_mmap_coherent(dev, area, vaddr, daddr, size);

	return 0;
}

static int snd_card_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime;
	struct snd_dma_buffer *dmab = NULL;
	struct snd_dma_buffer_kref *dmab_kref = NULL;
	size_t buffer_size, size;
	struct device *dev = substream->pcm->card->dev;
	phys_addr_t dat;
	void *vaddr;
	int ret;

	/* For mmap application, let size info of dmabuf to be PAGE_ALIGN. */
	buffer_size = PAGE_ALIGN(params_buffer_bytes(hw_params));

	pr_info("[ALSA %s demand %d Bytes PAGE_ALIGN %d Bytes]\n", __func__,
		params_buffer_bytes(hw_params), (int)buffer_size);

	if (PCM_SUBSTREAM_CHECK(substream))
		return -EINVAL;
	runtime = substream->runtime;

	if (runtime->dma_buffer_p) {
		dmab = runtime->dma_buffer_p;
		dmab_kref = container_of(dmab, struct snd_dma_buffer_kref, dmab);
		kref_put(&dmab_kref->ref, dmab_destroy);
	}

	dmab_kref = kzalloc(sizeof(*dmab_kref), GFP_KERNEL);
	if (!dmab_kref)
		return -ENOMEM;
	dmab_kref->dmab.dev = substream->dma_buffer.dev;
	dmab_kref->dmab.dev.dev = dev;

	/* Allocate buffer using ion */
	size = buffer_size;
	ret = rtk_snd_alloc(dev, size, (void *)&dat, (void **)&vaddr,
			    RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			    RTK_FLAG_ACPUACC);
	if (ret) {
		kfree(dmab_kref);
		return -ENOMEM;
	}

	dmab_kref->dmab.addr = dat;
	dmab_kref->dmab.area = vaddr;
	dmab_kref->dmab.bytes = buffer_size;
	kref_init(&dmab_kref->ref);

	snd_pcm_set_runtime_buffer(substream, &dmab_kref->dmab);
	runtime->dma_bytes = buffer_size;

	return 1;
}

static int snd_card_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_dma_buffer *dmab = NULL;
	struct snd_dma_buffer_kref *dmab_kref = NULL;

	pr_info("ALSA %s\n", __func__);

	if (PCM_SUBSTREAM_CHECK(substream))
		return -EINVAL;

	runtime = substream->runtime;
	if (!runtime->dma_area)
		return 0;

	if (runtime->dma_buffer_p) {
		/* Free buffer allocated by ion */
		pr_info("snd_dma_free_pages %s\n", __func__);
		dmab = runtime->dma_buffer_p;
		dmab_kref = container_of(dmab, struct snd_dma_buffer_kref, dmab);

		if (!atomic_read(&substream->mmap_count))
			kref_put(&dmab_kref->ref, dmab_destroy);
		else
			pr_err("%s mmap_count is not zero! dma still alive\n", __func__);
	}

	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static unsigned long ring_valid_data(unsigned long ring_base,
				     unsigned long ring_limit,
				     unsigned long ring_rp,
				     unsigned long ring_wp)
{
	if (ring_wp >= ring_rp)
		return (ring_wp - ring_rp);
	else
		return (ring_limit - ring_base) - (ring_rp - ring_wp);
}

static unsigned long ring_add(unsigned long ring_base,
			      unsigned long ring_limit,
			      unsigned long ptr,
			      unsigned int bytes)
{
	ptr += bytes;

	if (ptr >= ring_limit)
		ptr = ring_base + (ptr - ring_limit);

	return ptr;
}

static unsigned long ring_minus(unsigned long ring_base,
				unsigned long ring_limit,
				unsigned long ptr,
				int bytes)
{
	ptr -= bytes;

	if (ptr < ring_base)
		ptr = ring_limit - (ring_base - ptr);

	return ptr;
}

static unsigned long valid_free_size(unsigned long base,
				     unsigned long limit,
				     unsigned long rp,
				     unsigned long wp)
{
	/* -1 to avoid confusing full or empty */
	return (limit - base) - ring_valid_data(base, limit, rp, wp) - 1;
}

static int ring_check_ptr_valid_32(unsigned int ring_rp,
				   unsigned int ring_wp,
				   unsigned int ptr)
{
	if (ring_wp >= ring_rp)
		return (ptr < ring_wp && ptr >= ring_rp);
	else
		return (ptr >= ring_rp || ptr < ring_wp);
}

static unsigned long buf_memcpy2_ring(unsigned long base,
				      unsigned long limit,
				      unsigned long ptr,
				      char *buf,
				      unsigned long size)
{
	if (ptr + size <= limit) {
		memcpy((char *)ptr, buf, size);
	} else {
		int i = limit - ptr;

		memcpy((char *)ptr, (char *)buf, i);
		memcpy((char *)base, (char *)(buf + i), size - i);
	}

	ptr += size;
	if (ptr >= limit)
		ptr = base + (ptr - limit);

	return ptr;
}

static int rtk_create_pcm_instance(struct snd_card *card,
				   int instance_idx,
				   int playback_substreams,
				   int capture_substreams)
{
	struct snd_pcm *pcm = NULL;
	struct snd_pcm_substream *p = NULL;
	int i, err;

	pr_info("create pcm instance: play %d cap %d\n",
		playback_substreams, capture_substreams);

	err = snd_pcm_new(card
		, snd_pcm_id[instance_idx]
		, instance_idx
		, playback_substreams
		, capture_substreams
		, &pcm);
	if (err < 0) {
		pr_err("[%s snd_pcm_new fail]\n", __func__);
		return -EINVAL;
	}

	switch (instance_idx) {
	case ENUM_AIN_HDMIRX:
	case ENUM_AIN_I2S:
	case ENUM_AIN_NON_PCM:
	case ENUM_AIN_AEC_MIX:
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_PLAYBACK,
				&snd_card_rtk_playback_ops);
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_CAPTURE,
				&snd_card_rtk_capture_ops);
		sprintf(pcm->name, snd_pcm_id[instance_idx]);
		break;
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
	case ENUM_AIN_AUDIO_V4:
	case ENUM_AIN_I2S_LOOPBACK:
	case ENUM_AIN_DMIC_PASSTHROUGH:
	case ENUM_AIN_PURE_DMIC:
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_CAPTURE,
				&snd_card_rtk_capture_ops);
		sprintf(pcm->name, snd_pcm_id[instance_idx]);
		break;
	default:
		pr_err("[%s fail]\n", __func__);
		break;
	}

	/* construct hwdep for each device node */
	err = snd_rtk_create_hwdep_device(pcm);
	if (err < 0)
		return err;

	pcm->info_flags = 0;

	p = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	for (i = 0; i < playback_substreams; i++) {
		p->dma_buffer.dev.dev = pcm->card->dev;
		p->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_WC_SG;
		p = p->next;
	}

	p = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	for (i = 0; i < capture_substreams; i++) {
		p->dma_buffer.dev.dev = pcm->card->dev;
		p->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_WC_SG;
		p = p->next;
	}

	snd_pcm_add_chmap_ctls(pcm,
			       SNDRV_PCM_STREAM_PLAYBACK,
			       snd_pcm_alt_chmaps, 8, 0, NULL);

	return 0;
}

static void snd_rtk_playback_volume_work(struct work_struct *work)
{
	struct rtk_snd_mixer *mixer =
			container_of(work, struct rtk_snd_mixer, work_volume);

	rpc_set_volume(mixer->dev, mixer->mixer_volume[MIXER_ADDR_MASTER][0]);
}

static int rtk_snd_mixer_new_mixer(struct snd_card *card,
				   struct rtk_snd_mixer *mixer)
{
	unsigned int idx;
	int err;

	spin_lock_init(&mixer->mixer_lock);
	INIT_WORK(&mixer->work_volume, snd_rtk_playback_volume_work);

	strncpy(card->mixername, "RTK_Mixer", sizeof(card->mixername));

	/* add snc_control */
	for (idx = 0; idx < ARRAY_SIZE(rtk_snd_controls); idx++) {
		err = snd_ctl_add(card, snd_ctl_new1(&rtk_snd_controls[idx], mixer));
		if (err < 0) {
			pr_err("[snd_ctl_add faile %s]\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static int snd_RTK_volume_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;

	return 0;
}

static int snd_RTK_volume_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rtk_snd_mixer *mixer = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&mixer->mixer_lock, flags);
	switch (addr) {
	case MIXER_ADDR_MASTER:
		mixer->mixer_volume[addr][1] = mixer->mixer_volume[addr][0];
		break;
	default:
		break;
	}
	ucontrol->value.integer.value[0] = mixer->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = mixer->mixer_volume[addr][1];
	spin_unlock_irqrestore(&mixer->mixer_lock, flags);

	return 0;
}

static void ring1_to_ring2_general_64(struct audio_ringbuf_ptr_64 *ring1,
				      struct audio_ringbuf_ptr_64 *ring2,
				      long size)
{
	if (ring1->rp + size <= ring1->limit) {
		if (ring2->wp + size <= ring2->limit) {
			memcpy((char *)ring2->wp, (char *)ring1->rp, size);
		} else {
			int i = ring2->limit - ring2->wp;

			memcpy((char *)ring2->wp, (char *)ring1->rp, i);
			memcpy((char *)ring2->base, (char *)(ring1->rp + i), size - i);
		}
	} else {
		if (ring2->wp + size <= ring2->limit) {
			int i = ring1->limit - ring1->rp;

			memcpy((char *)ring2->wp, (char *)ring1->rp, i);
			memcpy((char *)(ring2->wp + i), (char *)(ring1->base), size - i);
		} else {
			int i, j;

			i = ring1->limit - ring1->rp;
			j = ring2->limit - ring2->wp;

			if (j <= i) {
				memcpy((char *)ring2->wp, (char *)ring1->rp, j);
				memcpy((char *)ring2->base, (char *)(ring1->rp + j), i - j);
				memcpy((char *)(ring2->base + i - j),
				       (char *)(ring1->base), size - i);
			} else {
				memcpy((char *)ring2->wp, (char *)ring1->rp, i);
				memcpy((char *)(ring2->wp + i), (char *)ring1->base, j - i);
				memcpy((char *)ring2->base,
				       (char *)(ring1->base + j - i), size - j);
			}
		}
	}

	ring1->rp += size;
	if (ring1->rp >= ring1->limit)
		ring1->rp = ring1->base + (ring1->rp - ring1->limit);

	ring2->wp += size;
	if (ring2->wp >= ring2->limit)
		ring2->wp = ring2->base + (ring2->wp - ring2->limit);
}

static int snd_RTK_volume_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rtk_snd_mixer *mixer = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, addr = kcontrol->private_value;
	int master;

	master = ucontrol->value.integer.value[0];
	if (master < 0)
		master = 0;

	if (master > 31)
		master = 31;

	spin_lock_irqsave(&mixer->mixer_lock, flags);
	change = mixer->mixer_volume[addr][0] != master;
	mixer->mixer_volume[addr][0] = master;
	mixer->mixer_volume[addr][1] = master;
	spin_unlock_irqrestore(&mixer->mixer_lock, flags);

	if (addr == MIXER_ADDR_MASTER)
		schedule_work(&mixer->work_volume);

	return change;
}

static int snd_RTK_capsrc_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int snd_RTK_capsrc_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rtk_snd_mixer *mixer = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&mixer->mixer_lock, flags);
	ucontrol->value.integer.value[0] = mixer->capture_source[addr][0];
	ucontrol->value.integer.value[1] = mixer->capture_source[addr][1];
	spin_unlock_irqrestore(&mixer->mixer_lock, flags);

	return 0;
}

static int snd_RTK_capsrc_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rtk_snd_mixer *mixer = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irqsave(&mixer->mixer_lock, flags);
	change = mixer->capture_source[addr][0] != left &&
			mixer->capture_source[addr][1] != right;
	mixer->capture_source[addr][0] = left;
	mixer->capture_source[addr][1] = right;
	spin_unlock_irqrestore(&mixer->mixer_lock, flags);

	return change;
}

#ifdef CONFIG_SYSFS
static ssize_t alsa_latency_show(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 char *buf)
{
	/* Return the last latency */
	return sprintf(buf, "%u\n", mtotal_latency);
}

static ssize_t alsa_active_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	/* Return current out buffer size */
	return sprintf(buf, "%u\n", rtk_dec_ao_buffer);
}

static ssize_t alsa_active_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	/* Setup decoder out buffer size */
	if (val >= 4096 && val <= (12 * 1024))
		rtk_dec_ao_buffer = val;
	else
		pr_err("set dec_ao_buffer failed! (size must be from 4k to 12k)\n");

	return count;
}

static struct kobj_attribute alsa_active_attr =
	__ATTR(dec_ao_buffer_size, 0644, alsa_active_show, alsa_active_store);

static struct kobj_attribute alsa_latency_attr =
	__ATTR(latency, 0444, alsa_latency_show, NULL);

static struct attribute *alsa_attrs[] = {
	&alsa_active_attr.attr,
	&alsa_latency_attr.attr,
	NULL,
};

static struct attribute_group rtk_alsa_attr_group = {
	.attrs = alsa_attrs,
};

static struct kobject *alsa_kobj;
static int __init alsa_sysfs_init(void)
{
	int ret;

	/* If the kobject was not able to be created, NULL will be returned */
	alsa_kobj = kobject_create_and_add("rtk_alsa", kernel_kobj);
	if (!alsa_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(alsa_kobj, &rtk_alsa_attr_group);
	if (ret) {
		kobject_del(alsa_kobj);
		kobject_put(alsa_kobj);
		alsa_kobj = NULL;
	}

	return ret;
}
#endif

static int snd_card_probe(struct platform_device *pdev)
{
	struct rtk_alsa_device *alsa_dev = NULL;
	struct snd_card *card = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct rtk_snd_mixer *mixer;
	int err;

	alsa_dev = devm_kzalloc(dev, sizeof(*alsa_dev), GFP_KERNEL);
	if (!alsa_dev)
		return -ENOMEM;
	alsa_dev->dev = dev;

	if (WARN_ON(!np))
		dev_err(dev, "can not found device node\n");

	if (IS_ENABLED(CONFIG_RPMSG_RTK_RPC)) {
		hifi_ept_info = of_krpc_ept_info_get(np, 1);
		if (IS_ERR(hifi_ept_info))
			return dev_err_probe(dev, PTR_ERR(hifi_ept_info),
					     "failed to get HIFI krpc ept info: 0x%lx\n",
					     PTR_ERR(hifi_ept_info));

		snd_ept_init(hifi_ept_info);
	}

	/* Get virtual mapping for clk enable 2 */
	sys_clk_en2_virt = of_iomap(np, 0);

	err = snd_card_new(dev, -1, "rtk_snd_hifi",
			   THIS_MODULE, sizeof(struct rtk_snd_mixer),
			   &card);
	if (err < 0) {
		pr_err("snd_card_new fail\n");
		return -EINVAL;
	}

	/* set up dma buffer operation */
	set_dma_ops(card->dev, &rheap_dma_ops);

	alsa_dev->card = card;
	mixer = (struct rtk_snd_mixer *)card->private_data;
	mixer->dev = dev;

	err = rtk_create_pcm_instance(card, ENUM_AIN_HDMIRX, 2, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_I2S, 1, 1);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_NON_PCM, 1, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_AEC_MIX, 1, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_AUDIO_V2, 0, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_AUDIO_V3, 0, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_AUDIO_V4, 0, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_I2S_LOOPBACK, 0, 1);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_DMIC_PASSTHROUGH, 0, 0);
	err |= rtk_create_pcm_instance(card, ENUM_AIN_PURE_DMIC, 0, 1);
	if (err < 0) {
		pr_err("[%s create instance fail]\n", __func__);
		goto __nodev;
	}

	err = rtk_snd_mixer_new_mixer(card, mixer);
	if (err < 0) {
		pr_err("[%s add mixer fail]\n", __func__);
		goto __nodev;
	}

	strncpy(card->driver, "rtk_snd_hifi", sizeof(card->driver));
	strncpy(card->shortname, "rtk_snd_hifi", sizeof(card->shortname));
	strncpy(card->longname, "rtk_snd_hifi", sizeof(card->longname));

	/* Init suspend variable */
	is_suspend = false;

	/* Init parameter */
	snd_open_ai_count = 0;
	snd_open_count = 0;
	mtotal_latency = 0;
	rtk_dec_ao_buffer = RTK_DEC_AO_BUFFER_SIZE;

#ifdef CONFIG_SYSFS
	err = alsa_sysfs_init();
	if (err)
		pr_err("%s: unable to create sysfs entry\n", __func__);
#endif

	err = snd_card_register(card);
	if (err) {
		pr_err("[%s card register fail]\n", __func__);
		goto __nodev;
	}

	platform_set_drvdata(pdev, alsa_dev);
	dev_info(dev, "initialized\n");
	return 0;
__nodev:
	snd_card_free(card);
	return err;
}

static int snd_card_remove(struct platform_device *pdev)
{
	struct rtk_alsa_device *alsa_dev = platform_get_drvdata(pdev);

	if (alsa_dev->card)
		snd_card_free(alsa_dev->card);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int rtk_alsa_suspend(struct device *pdev)
{
	struct rtk_alsa_device *alsa_dev = dev_get_drvdata(pdev);
	struct snd_device *dev;
	struct snd_pcm *pcm;
	int err = 0;

	snd_power_change_state(alsa_dev->card, SNDRV_CTL_POWER_D3hot);
	is_suspend = true;

	/* Only the pcm's name has "rtk_name" need the suspend */
	list_for_each_entry(dev, &alsa_dev->card->devices, list) {
		pcm = dev->device_data;
		if (strstr(pcm->name, "rtk_snd"))
			err = snd_pcm_suspend_all(pcm);
		if (err < 0)
			pr_err("%s pcm suspend fail\n", __func__);
	}

	return 0;
}

static int rtk_alsa_resume(struct device *pdev)
{
	struct rtk_alsa_device *alsa_dev = dev_get_drvdata(pdev);

	snd_power_change_state(alsa_dev->card, SNDRV_CTL_POWER_D0);
	is_suspend = false;

	return 0;
}

static const struct dev_pm_ops rtk_alsa_pm = {
	.resume = rtk_alsa_resume,
	.suspend = rtk_alsa_suspend,
};

static const struct of_device_id rtk_pcm_dt_match[] = {
	{ .compatible = "realtek,rtk-alsa-hifi" },
	{}
};

static struct platform_driver rtk_alsa_driver = {
	.probe =	snd_card_probe,
	.remove =	snd_card_remove,
	.driver = {
		.name =		"rtk_snd_hifi",
		.pm =		&rtk_alsa_pm,
		.of_match_table = rtk_pcm_dt_match,
	},
};

static int __init RTK_alsa_card_init(void)
{
	int err;

	/* Init kobj */
	alsa_kobj = NULL;

	err = platform_driver_register(&rtk_alsa_driver);
	if (err < 0)
		goto RETURN_ERR;

	return 0;

RETURN_ERR:
	pr_err("%s error\n", __func__);

	return err;
}

static void __exit RTK_alsa_card_exit(void)
{
	if (sys_clk_en2_virt) {
		iounmap(sys_clk_en2_virt);
		sys_clk_en2_virt  = NULL;
	}

	platform_driver_unregister(&rtk_alsa_driver);

#ifdef CONFIG_SYSFS
	if (alsa_kobj) {
		kobject_del(alsa_kobj);
		kobject_put(alsa_kobj);
		alsa_kobj = NULL;
	}
#endif
}
module_init(RTK_alsa_card_init);
module_exit(RTK_alsa_card_exit);

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
