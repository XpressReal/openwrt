/* SPDX-License-Identifier: GPL-2.0+
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#ifndef SND_HIFI_REALTEK_H
#define SND_HIFI_REALTEK_H

#include <sound/pcm.h>
#include "snd-realtek_common.h"
#include "snd_rtk_audio_enum.h"
#include <soc/realtek/rtk-krpc-agent.h>

extern struct rtk_krpc_ept_info *hifi_ept_info;

/* playback */
#define RTK_DMP_PLAYBACK_INFO (SNDRV_PCM_INFO_INTERLEAVED | \
				SNDRV_PCM_INFO_NONINTERLEAVED | \
				SNDRV_PCM_INFO_RESUME | \
				SNDRV_PCM_INFO_MMAP | \
				SNDRV_PCM_INFO_MMAP_VALID | \
				SNDRV_PCM_INFO_PAUSE)

#define RTK_DMP_PLAYBACK_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
				SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_S24_3LE)

#define RTK_DMP_PLYABACK_RATES (SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_32000 | \
				SNDRV_PCM_RATE_44100 | \
				SNDRV_PCM_RATE_48000 | \
				SNDRV_PCM_RATE_88200 | \
				SNDRV_PCM_RATE_96000 | \
				SNDRV_PCM_RATE_176400 | \
				SNDRV_PCM_RATE_192000)

#define RTK_DMP_PLAYBACK_RATE_MIN       16000
#define RTK_DMP_PLAYBACK_RATE_MAX       192000
#define RTK_DMP_PLAYBACK_CHANNELS_MIN   1
#define RTK_DMP_PLAYBACK_CHANNELS_MAX   8
#define RTK_DMP_PLAYBACK_MAX_BUFFER_SIZE         (192 * 1024)
#define RTK_DMP_PLAYBACK_MIN_PERIOD_SIZE         1024
#define RTK_DMP_PLAYBACK_MAX_PERIOD_SIZE         (24 * 1024)
#define RTK_DMP_PLAYBACK_PERIODS_MIN             4
#define RTK_DMP_PLAYBACK_PERIODS_MAX             1024
#define RTK_DMP_PLAYBACK_FIFO_SIZE               32

/* capture */
#define RTK_DMP_CAPTURE_INFO RTK_DMP_PLAYBACK_INFO
#define RTK_DMP_CAPTURE_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_S24_3LE | \
				SNDRV_PCM_FMTBIT_S32_LE)

#define RTK_DMP_CAPTURE_RATES (SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_44100 | \
				SNDRV_PCM_RATE_48000)

#define RTK_DMP_CAPTURE_RATE_MIN 16000
#define RTK_DMP_CAPTURE_RATE_MAX 48000
#define RTK_DMP_CAPTURE_CHANNELS_MIN 2
#define RTK_DMP_CAPTURE_CHANNELS_MAX 2
#define RTK_DMP_CAPTURE_MAX_BUFFER_SIZE         RTK_DMP_PLAYBACK_MAX_BUFFER_SIZE
#define RTK_DMP_CAPTURE_MIN_PERIOD_SIZE         RTK_DMP_PLAYBACK_MIN_PERIOD_SIZE
#define RTK_DMP_CAPTURE_MAX_PERIOD_SIZE         (16 * 1024)
#define RTK_DMP_CAPTURE_PERIODS_MIN             RTK_DMP_PLAYBACK_PERIODS_MIN
#define RTK_DMP_CAPTURE_PERIODS_MAX             RTK_DMP_PLAYBACK_PERIODS_MAX
#define RTK_DMP_CAPTURE_FIFO_SIZE               RTK_DMP_PLAYBACK_FIFO_SIZE

#define S_OK 0x10000000
#define RTK_DEC_AO_BUFFER_SIZE          (7 * 1024)
#define RTK_ENC_AI_BUFFER_SIZE          (32 * 1024)
#define RTK_ENC_LPCM_BUFFER_SIZE        (32 * 1024)
#define RTK_ENC_PTS_BUFFER_SIZE         (8 * 1024)

enum CAPTURE_TYPE {
	ENUM_AIN_HDMIRX = 0,
	ENUM_AIN_I2S = 1,
	ENUM_AIN_NON_PCM = 2,
	ENUM_AIN_AEC_MIX = 3,
	ENUM_AIN_AUDIO_V2 = 4,
	ENUM_AIN_AUDIO_V3 = 5,
	ENUM_AIN_AUDIO_V4 = 6,
	ENUM_AIN_I2S_LOOPBACK = 7,
	ENUM_AIN_DMIC_PASSTHROUGH = 8,
	ENUM_AIN_PURE_DMIC = 9,
};

enum PLAYBACK_TYPE {
	ENUM_AO_DECODER = 0,
	ENUM_AO_SKIP_DECODER = 1,
};

#define MAX_PCM_DEVICES     1
#define MAX_PCM_SUBSTREAMS  3
#define MAX_AI_DEVICES      2

#define MIXER_ADDR_MASTER   0
#define MIXER_ADDR_LINE     1
#define MIXER_ADDR_MIC      2
#define MIXER_ADDR_SYNTH    3
#define MIXER_ADDR_CD       4
#define MIXER_ADDR_LAST     4

#define RTK_VOLUME(xname, xindex, addr)    \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_RTK_volume_info, \
	.get = snd_RTK_volume_get, \
	.put = snd_RTK_volume_put, \
	.private_value = addr \
}

#define RTK_CAPSRC(xname, xindex, addr)    \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.index = xindex, \
	.info = snd_RTK_capsrc_info, \
	.get = snd_RTK_capsrc_get, \
	.put = snd_RTK_capsrc_put, \
	.private_value = addr \
}

/************************************************************************/
/* ENUM                                                                 */
/************************************************************************/
struct audio_config_command {
	enum AUDIO_CONFIG_CMD_MSG msd_id;
	unsigned int value[6];
};

struct rpcres_long {
	u32 result;
	int data;
};

struct audio_rpc_privateinfo_parameters {
	int instance_id;
	enum AUDIO_ENUM_PRIVAETINFO type;
	volatile int private_info[16];
};

struct audio_rpc_privateinfo_returnval {
	int instance_id;
	volatile int private_info[16];
};

struct audio_rpc_instance {
	int instance_id;
	int type;
	int reserved[30];
};

struct rpc_create_ao_agent_t {
	struct audio_rpc_instance info;
	struct rpcres_long retval;
	u32 ret;
};

struct rpc_create_pcm_decoder_ctrl_t {
	struct audio_rpc_instance instance;
	struct rpcres_long res;
	u32 ret;
};

struct audio_rpc_ringbuffer_header {
	int instance_id;
	int pin_id;
	int ringbuffer_header_list[8];
	int read_idx;
	int list_size;
	int reserved[20];
};

#ifdef CONFIG_RTK_CACHEABLE_HEADER
/* Ring Buffer header structure (cacheable) */
struct ringbuffer_header {
	/* align 128 bytes */
	unsigned int write_ptr;
	unsigned char w_reserved[124];

	/* align 128 bytes */
	unsigned int read_ptr[4];
	unsigned char  r_reserved[112];

	unsigned int magic;
	unsigned int begin_addr;
	unsigned int size;
	unsigned int buffer_id;
	unsigned int num_read_ptr;
	unsigned int reserve2;
	unsigned int reserve3;

	int          file_offset;
	int          requested_file_offset;
	int          file_size;
	int          seekable;

	unsigned char  readonly[84];
};
#else
/* Ring Buffer header is the shared memory structure */
struct ringbuffer_header {
	unsigned int magic;
	unsigned int begin_addr;
	unsigned int size;
	unsigned int buffer_id;

	unsigned int write_ptr;
	unsigned int num_read_ptr;
	unsigned int reserve2;
	unsigned int reserve3;

	unsigned int read_ptr[4];

	int          file_offset;
	int          requested_file_offset;
	int          file_size;

	int          seekable;
/*
 * file_offset:
 * the offset to the streaming file from the beginning of the file.
 * It is set by system to tell FW that the current streaming is starting from file_offset bytes.
 * For example, the TIFF file display will set file_offset to 0 at beginning.
 *
 * requested_file_offset:
 * the offset to be set by video firmware, to request system to seek to other place.
 * The initial is -1.When it is not equal to -1, that means FW side is requesting a new seek.
 *
 * file_size:
 * file size. At current implementation, only TIFF decode needs the file_size,
 * other decoding does not pay attention to this field
 *
 * the behavior for TIFF seek:
 * At the initial value, file_offset = 0, or at any initial offset
 *(for example, resume from bookmark), requested_file_offset=-1. file_size= file size.
 * 1. If FW needs to perform seek operation,
 *    FW set requested_file_offset to the value it need to seek.
 * 2. Once system see RequestedOffset is not -1,
 *    system reset the ring buffer
 *    (FW need to make sure it will not use ring buffer after request the seek),
 *    set file_offset to the new location (the value of requested_file_offset),
 *    then set RequestedOffset to -1. From now on,
 *    system will stream data from byte file_offset of the file.
 * 3. FW needs to wait until RequestedOffset== -1,
 *    then check the value inside file_offset.
 *    If file_offset is -1, that means read out of bound.
 *    If system already finish the streaming before FW issue a seek,
 *    system will still continue polling.
 */
};
#endif

struct rpc_initringbuffer_header_t {
	struct audio_rpc_ringbuffer_header header;
	struct rpcres_long ret;
	u32 res;
};

struct audio_rpc_connection {
	int src_instance_id;
	int src_pin_id;
	int des_instance_id;
	int des_pin_id;
	int media_type;
	int reserved[27];
};

struct rpc_connection_t {
	struct audio_rpc_connection out;
	struct rpcres_long ret;
	u32 res;
};

struct rpc_t {
	int inst_id;
	int reserved[31];
	struct rpcres_long retval;
	u32 res;
};

struct rpc_pause_t {
	int inst_id;
	int reserved[31];
	u32 retval;
	u32 res;
};

struct audio_rpc_sendio {
	int instance_id;
	int pin_id;
	int reserved[30];
};

struct rpc_flash_t {
	struct audio_rpc_sendio sendio;
	struct rpcres_long retval;
	u32 res;
};

struct audio_inband_cmd_pkt_header {
	enum AUDIO_INBAND_CMD_TYPE type;
	int size;
};

/* private_info[6] is used for choosing decoder sync pts method */
struct audio_dec_new_format {
	struct audio_inband_cmd_pkt_header header;
	unsigned int w_ptr;
	enum AUDIO_DEC_TYPE audio_type;
	int private_info[8];
};

struct audio_dec_pts_info {
	struct audio_inband_cmd_pkt_header header;
	unsigned int w_ptr;
	unsigned int PTSH;
	unsigned int PTSL;
};

struct rpc_stop_t {
	int instance_id;
	int reserved[31];
	struct rpcres_long retval;
	u32 res;
};

struct rpc_destroy_t {
	int instance_id;
	int reserved[31];
	struct rpcres_long retval;
	u32 res;
};

struct rpc_get_volume_t {
	struct rpcres_long res;
	u32 ret;
	struct audio_rpc_privateinfo_parameters param;
};

struct audio_ringbuf_ptr_64 {
	unsigned long base;
	unsigned long limit;
	unsigned long cp;
	unsigned long rp;
	unsigned long wp;
};

struct alsa_mem_info {
	phys_addr_t p_phy;
	unsigned int *p_virt;
	unsigned int size;
};

struct alsa_latency_info {
	unsigned int latency;
	unsigned int ptsl;
	unsigned int ptsh;
	unsigned int sum; /* latency + ptsL */
	unsigned int decin_wp;
	unsigned int sync;
	unsigned int dec_in_delay;
	unsigned int dec_out_delay;
	unsigned int ao_delay;
	int rvd[8];
};

struct audio_rpc_aio_privateinfo_parameters {
	int instance_id;
	enum AUDIO_ENUM_AIO_PRIVAETINFO type;
	int argate_info[16];
};

struct audio_rpc_dec_privateinfo_parameters {
	long instance_id;
	enum AUDIO_ENUM_DEC_PRIVAETINFO type;
	long private_info[16];
};

/* rtk sound mixer */
struct rtk_snd_mixer {
	struct device *dev;
	/* lock for controling mixer */
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST + 1][2];
	int mixer_change;
	int capture_source[MIXER_ADDR_LAST + 1][2];
	int capture_change;
	struct work_struct work_volume;
	struct snd_compr *compr;
};

/* RTK PCM instance */
struct snd_rtk_pcm {
	/* inband ring at the first, because of 128 align for HIFI */
	unsigned int dec_inband_data[64];
	struct ringbuffer_header *dec_out_ring[8];
	struct rtk_snd_mixer *mixer;
	struct snd_pcm_substream *substream;
	struct ringbuffer_header *dec_inband_ring;
	struct ringbuffer_header *dec_inring;

	snd_pcm_uframes_t hw_ptr;
	snd_pcm_uframes_t prehw_ptr;
	snd_pcm_uframes_t total_read;
	snd_pcm_uframes_t total_write;

#ifdef DEBUG_RECORD
	struct file *fp;
	loff_t pos;
	mm_segment_t fs;
#endif

	int dec_agent_id;
	int dec_pin_id;
	int ao_agent_id;
	int ao_pin_id;
	int volume;
	int init_ring;
	int hw_init;
	int last_channel;
	int ao_decode_lpcm;
	int dec_out_msec;
	int *g_sharemem_ptr;
	struct alsa_latency_info *g_sharemem_ptr2;
	struct alsa_latency_info *g_sharemem_ptr3;
	unsigned int period_bytes;
	unsigned int ring_size;
	unsigned int dbg_count;
	phys_addr_t phy_dec_out_data[8];
	phys_addr_t phy_addr;
	phys_addr_t phy_addr_rpc;
	phys_addr_t g_sharemem_ptr_dat;
	phys_addr_t g_sharemem_ptr_dat2;
	phys_addr_t g_sharemem_ptr_dat3;
	phys_addr_t phy_dec_inring;
	phys_addr_t phy_dec_inband_ring;
	phys_addr_t phy_dec_out_ring[8];
	void *vaddr_rpc;
	void *vaddr_dec_out_data[8];
	/* lock for playback */
	spinlock_t playback_lock;
};

struct snd_rtk_cap_pcm {
	struct rtk_snd_mixer *mixer;
	struct snd_pcm_substream *substream;
	struct ringbuffer_header *airing[8];
	struct ringbuffer_header *lpcm_ring;
	struct ringbuffer_header *pts_ring_hdr;
	snd_pcm_uframes_t total_write;
	enum AUDIO_FORMAT_OF_AI_SEND_TO_ALSA ai_format;

	/* hrtimer member */
	struct hrtimer hr_timer;
	enum hrtimer_restart en_hr_timer;
	ktime_t ktime;

	int ai_agent_id;
	int ao_agent_id;
	int ao_pin_id;
	int init_ring;
	int source_in;
	int dmic_volume[2];
	unsigned int *lpcm_data;
	unsigned int period_bytes;
	unsigned int frame_bytes;
	unsigned int ring_size;
	unsigned int lpcm_ring_size;
	phys_addr_t phy_airing_data[8];
	phys_addr_t phy_lpcm_data;
	phys_addr_t phy_addr;
	phys_addr_t phy_addr_rpc;
	phys_addr_t phy_airing[8];
	phys_addr_t phy_pts_ring_hdr;
	phys_addr_t phy_lpcm_ring;
	struct alsa_mem_info pts_mem;
	struct audio_ringbuf_ptr_64 pts_ring;
	struct timespec64 ts;
	void *vaddr_rpc;
	void *vaddr_airing_data[8];
	size_t size_airing[8];
	/* lock for capture */
	spinlock_t capture_lock;
};

struct snd_pcm_mmap_fd {
	s32 dir;
	s32 fd;
	s32 size;
	s32 actual_size;
};

struct snd_dma_buffer_kref {
	struct snd_dma_buffer dmab;
	struct kref ref;
};

#define SNDRV_PCM_IOCTL_GET_LATENCY  _IOR('A', 0xF0, int)
#define SNDRV_PCM_IOCTL_GET_FW_DELAY _IOR('A', 0xF1, unsigned int)
#define SNDRV_PCM_IOCTL_MMAP_DATA_FD _IOWR('A', 0xE4, struct snd_pcm_mmap_fd)
#define SNDRV_PCM_IOCTL_DMIC_VOL_SET _IOW('A', 0xE6, int)
/************************************************************************/
/* PROTOTYPE                                                            */
/************************************************************************/
int snd_realtek_hw_ring_write(struct ringbuffer_header *ring,
			      void *data, int len, unsigned int offset);
int write_inband_cmd(struct snd_rtk_pcm *dpcm, void *data, int len);
int snd_ept_init(struct rtk_krpc_ept_info *krpc_ept_info);

/* RPC function */
int rpc_connect_svc(phys_addr_t paddr, void *vaddr,
		    struct audio_rpc_connection *pconnection);
int rpc_destroy_svc(phys_addr_t paddr, void *vaddr, int instance_id);
int rpc_flush_svc(phys_addr_t paddr, void *vaddr, struct audio_rpc_sendio *sendio);
int rpc_initringbuffer_header(phys_addr_t paddr, void *vaddr,
			      struct audio_rpc_ringbuffer_header *header, int buffer_count);
int rpc_pause_svc(phys_addr_t paddr, void *vaddr, int instance_id);
int rpc_run_svc(phys_addr_t paddr, void *vaddr, int instance_id);
int rpc_stop_svc(phys_addr_t paddr, void *vaddr, int instance_id);
int rpc_create_ao_agent(phys_addr_t paddr, void *vaddr, int *ao_id, int pin_id);
int rpc_get_ao_flash_pin(phys_addr_t paddr, void *vaddr, int ao_agent_id);
int rpc_create_decoder_agent(phys_addr_t paddr, void *vaddr, int *dec_id, int *pin_id);
int rpc_set_ao_flash_volume(phys_addr_t paddr, void *vaddr, struct snd_rtk_pcm *dpcm);
int rpc_release_ao_flash_pin(phys_addr_t paddr, void *vaddr, int ao_agent_id, int ao_pin_id);
int rpc_set_volume(struct device *dev, int volume);
int rpc_get_volume(phys_addr_t paddr, void *vaddr);
int rpc_put_share_memory_latency(phys_addr_t paddr, void *vaddr,
				 void *p, void *p2, int dec_id, int ao_id, int type);
int rpc_create_ai_agent(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_ai_connect_alsa(phys_addr_t paddr, void *vaddr, struct snd_pcm_runtime *runtime);
int rpc_ai_config_hdmi_rx_in(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_ai_config_i2s_in(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_ai_config_audio_in(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_ai_config_i2s_loopback_in(phys_addr_t paddr, void *vaddr,
				  struct snd_rtk_cap_pcm *dpcm);
int rpc_ai_config_dmic_in(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_destroy_ai_flow_svc(phys_addr_t paddr, void *vaddr,
			    int instance_id, int hw_configed);
int rpc_ai_config_nonpcm_in(phys_addr_t paddr, void *vaddr,
			    struct snd_rtk_cap_pcm *dpcm);
int rpc_set_ai_flash_volume(phys_addr_t paddr, void *vaddr,
			    struct snd_rtk_cap_pcm *dpcm, unsigned int volume);
int rpc_ao_config_without_decoder(phys_addr_t paddr, void *vaddr, struct snd_pcm_runtime *runtime);
int rpc_create_global_ao(phys_addr_t paddr, void *vaddr, int *ao_id);
int rpc_ai_connect_ao(phys_addr_t paddr, void *vaddr, struct snd_rtk_cap_pcm *dpcm);
int rpc_set_max_latency(phys_addr_t paddr, void *vaddr, struct snd_rtk_pcm *dpcm);
#endif /* SND_HIFI_REALTEK_H */
