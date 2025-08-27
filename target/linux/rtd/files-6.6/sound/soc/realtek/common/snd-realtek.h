/* SPDX-License-Identifier: GPL-2.0+
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#ifndef SND_REALTEK_H
#define SND_REALTEK_H

#include <sound/pcm.h>
#include "snd-realtek_common.h"
#include "snd_rtk_audio_enum.h"
#include <soc/realtek/rtk-krpc-agent.h>

extern struct rtk_krpc_ept_info *acpu_ept_info;

//////////////////
// Main Control //
//////////////////
#define CAPTURE_USE_PTS_RING

#define S_OK 0x10000000
#define CONVERT_FOR_AVCPU(x) (x)
#define AUDIO_ION_FLAG (ION_USAGE_MMAP_WRITECOMBINE | ION_FLAG_SCPUACC | ION_FLAG_ACPUACC)

//////////////
// playback //
//////////////
#define RTK_DMP_PLAYBACK_INFO (SNDRV_PCM_INFO_INTERLEAVED | \
								SNDRV_PCM_INFO_NONINTERLEAVED | \
								SNDRV_PCM_INFO_RESUME | \
								SNDRV_PCM_INFO_MMAP | \
								SNDRV_PCM_INFO_MMAP_VALID | \
								SNDRV_PCM_INFO_PAUSE)
#define RTK_DMP_PLAYBACK_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE)
#define RTK_DMP_PLYABACK_RATES (SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 \
					 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 \
					 | SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)
#define RTK_DMP_PLAYBACK_RATE_MIN       16000
#define RTK_DMP_PLAYBACK_RATE_MAX       192000
#define RTK_DMP_PLAYBACK_CHANNELS_MIN   1
#define RTK_DMP_PLAYBACK_CHANNELS_MAX   8
#define RTK_DMP_PLAYBACK_MAX_BUFFER_SIZE         (192*1024)
#define RTK_DMP_PLAYBACK_MIN_PERIOD_SIZE         64
#define RTK_DMP_PLAYBACK_MAX_PERIOD_SIZE         (24*1024)
#define RTK_DMP_PLAYBACK_PERIODS_MIN             4
#define RTK_DMP_PLAYBACK_PERIODS_MAX             1024
#define RTK_DMP_PLAYBACK_FIFO_SIZE               32

/////////////
// capture //
/////////////
#define RTK_DMP_CAPTURE_INFO RTK_DMP_PLAYBACK_INFO
#define RTK_DMP_CAPTURE_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)
#define RTK_DMP_CAPTURE_RATES (SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RTK_DMP_CAPTURE_RATE_MIN 16000
#define RTK_DMP_CAPTURE_RATE_MAX 48000
#define RTK_DMP_CAPTURE_CHANNELS_MIN 2
#define RTK_DMP_CAPTURE_CHANNELS_MAX 2
#define RTK_DMP_CAPTURE_MAX_BUFFER_SIZE         RTK_DMP_PLAYBACK_MAX_BUFFER_SIZE
#define RTK_DMP_CAPTURE_MIN_PERIOD_SIZE         RTK_DMP_PLAYBACK_MIN_PERIOD_SIZE
#define RTK_DMP_CAPTURE_MAX_PERIOD_SIZE         16*1024
#define RTK_DMP_CAPTURE_PERIODS_MIN             RTK_DMP_PLAYBACK_PERIODS_MIN
#define RTK_DMP_CAPTURE_PERIODS_MAX             RTK_DMP_PLAYBACK_PERIODS_MAX
#define RTK_DMP_CAPTURE_FIFO_SIZE               RTK_DMP_PLAYBACK_FIFO_SIZE

#define RTK_DEC_AO_BUFFER_SIZE          (7*1024)//(10*1024)
#define RTK_ENC_AI_BUFFER_SIZE          (32*1024)//(64*1024)
#define RTK_ENC_LPCM_BUFFER_SIZE        (32*1024)
#define RTK_ENC_PTS_BUFFER_SIZE         (8*1024)

enum {
	ENUM_AIN_HDMIRX = 0,
	ENUM_AIN_I2S,  // from ADC outside of IC
	ENUM_AIN_AUDIO,
	ENUM_AIN_AUDIO_V2,
	ENUM_AIN_AUDIO_V3,
	ENUM_AIN_AUDIO_V4,
	ENUM_AIN_I2S_LOOPBACK,
	ENUM_AIN_DMIC_PASSTHROUGH,
	ENUM_AIN_PURE_DMIC
};

////////////////////////////////////////////
////////////////////////////////////////////

#define MAX_PCM_DEVICES     1 //2 todo
#define MAX_PCM_SUBSTREAMS  3
#define MAX_AI_DEVICES      2

#define MIXER_ADDR_MASTER   0
#define MIXER_ADDR_LINE     1
#define MIXER_ADDR_MIC      2
#define MIXER_ADDR_SYNTH    3
#define MIXER_ADDR_CD       4
#define MIXER_ADDR_LAST     4

#define MARS_VOLUME(xname, xindex, addr)    \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
	.info = snd_RTK_volume_info,\
	.get = snd_RTK_volume_get, .put = snd_RTK_volume_put,\
	.private_value = addr }

#define MARS_CAPSRC(xname, xindex, addr)    \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
	.info = snd_RTK_capsrc_info,\
	.get = snd_RTK_capsrc_get, .put = snd_RTK_capsrc_put,\
	.private_value = addr }

#define RING32_2_RING64(pRing64, pRing32) \
{\
	pRing64->base = (unsigned long)pRing32->base; \
	pRing64->limit = (unsigned long)pRing32->limit; \
	pRing64->wp = (unsigned long)pRing32->wp; \
	pRing64->rp = (unsigned long)pRing32->rp; \
	pRing64->cp = (unsigned long)pRing32->cp; \
}

/************************************************************************/
/* ENUM                                                                 */
/************************************************************************/
struct AUDIO_CONFIG_COMMAND {
	enum AUDIO_CONFIG_CMD_MSG msgID;
	unsigned int value[6];
};

struct RPCRES_LONG {
	uint32_t result;
	int data;
};

struct RPC_DEFAULT_INPUT_T {
	unsigned int  info;
	struct RPCRES_LONG retval;
	uint32_t ret;
};

struct AUDIO_RPC_PRIVATEINFO_PARAMETERS {
	int instanceID;
	enum AUDIO_ENUM_PRIVAETINFO type;
	volatile int privateInfo[16];
};

struct AUDIO_RPC_PRIVATEINFO_RETURNVAL {
	int instanceID;
	volatile int privateInfo[16];
};

struct AUDIO_RPC_INSTANCE {
	int instanceID;
	int type;
};

struct RPC_CREATE_AO_AGENT_T {
	struct AUDIO_RPC_INSTANCE info;
	struct RPCRES_LONG retval;
	uint32_t ret;
};

struct RPC_CREATE_PCM_DECODER_CTRL_T {
	struct AUDIO_RPC_INSTANCE instance;
	struct RPCRES_LONG res;
	uint32_t ret;
};

struct AUDIO_RPC_RINGBUFFER_HEADER {
	int instanceID;
	int pinID;
	int pRingBufferHeaderList[8];
	int readIdx;
	int listSize;
};

#ifdef CONFIG_RTK_CACHEABLE_HEADER
// Ring Buffer header structure (cacheable)
struct RINGBUFFER_HEADER {
	//align 128 bytes
	unsigned int writePtr;
	unsigned char w_reserved[124];

	//align 128 bytes
	unsigned int readPtr[4];
	unsigned char  r_reserved[112];

	unsigned int magic;   //Magic number
	unsigned int beginAddr;
	unsigned int size;
	unsigned int bufferID;  // RINGBUFFER_TYPE, choose a type from RINGBUFFER_TYPE
	unsigned int numOfReadPtr;
	unsigned int reserve2;  //Reserve for Red Zone
	unsigned int reserve3;  //Reserve for Red Zone

	int          fileOffset;
	int          requestedFileOffset;
	int          fileSize;
	int          bSeekable;  /* Can't be sought if data is streamed by HTTP */

	unsigned char  readonly[84];
};
#else
// Ring Buffer header is the shared memory structure
struct RINGBUFFER_HEADER {
	unsigned int magic;   //Magic number
	unsigned int beginAddr;
	unsigned int size;
	unsigned int bufferID;  // RINGBUFFER_TYPE, choose a type from RINGBUFFER_TYPE

	unsigned int writePtr;
	unsigned int numOfReadPtr;
	unsigned int reserve2;  //Reserve for Red Zone
	unsigned int reserve3;  //Reserve for Red Zone

	unsigned int readPtr[4];

	int          fileOffset;
	int          requestedFileOffset;
	int          fileSize;

	int          bSeekable;  /* Can't be sought if data is streamed by HTTP */
/*
 * FileOffset:
 * the offset to the streaming file from the beginning of the file.
 * It is set by system to tell FW that the current streaming is starting from ��FileOffset�� bytes.
 * For example, the TIFF file display will set fileOffset to 0 at beginning.
 *
 * RequestedFileOffset:
 * the offset to be set by video firmware, to request system to seek to other place.
 * The initial is -1.When it is not equal to -1, that means FW side is requesting a new seek.
 *
 * FileSize:
 * file size. At current implementation, only TIFF decode needs the fileSize,
 * other decoding does not pay attention to this field
 *
 * the behavior for TIFF seek:
 * At the initial value, FileOffset = 0, or at any initial offset (for example, resume from bookmark), RequestedFileOffset=-1. FileSize= file size.
 * 1. If FW needs to perform seek operation, FW set RequestedFileOffset to the value it need to seek.
 * 2. Once system see RequestedOffset is not -1, system reset the ring buffer (FW need to make sure it will not use ring buffer after request the seek), set FileOffset to the new location (the value of RequestedFileOffset), then set RequestedOffset  to -1. From now on, system will stream data from byte FileOffset of the file.
 * 3. FW needs to wait until RequestedOffset== -1, then check the value inside FileOffset. If FileOffset is -1, that means read out of bound.
 * If system already finish the streaming before FW issue a seek, system will still continue polling.
 */
};
#endif

struct RPC_INITRINGBUFFER_HEADER_T {
	struct RPCRES_LONG ret;
	uint32_t res;
	struct AUDIO_RPC_RINGBUFFER_HEADER header;
};

struct AUDIO_RPC_CONNECTION {
	int srcInstanceID;
	int srcPinID;
	int desInstanceID;
	int desPinID;
	int mediaType;
};

struct RPC_CONNECTION_T {
	struct AUDIO_RPC_CONNECTION out;
	struct RPCRES_LONG ret;
	uint32_t res;
};

struct RPC_TOAGENT_T {
	int inst_id;
	struct RPCRES_LONG retval;
	uint32_t res;
};

struct RPC_TOAGENT_PAUSE_T {
	int inst_id;
	uint32_t retval;
	uint32_t res;
};

struct AUDIO_RPC_SENDIO {
	int instanceID;
	int pinID;
};

struct RPC_TOAGENT_FLASH_T {
	struct RPCRES_LONG retval;
	uint32_t res;
	struct AUDIO_RPC_SENDIO sendio;
};

struct AUDIO_INBAND_CMD_PKT_HEADER {
	enum AUDIO_INBAND_CMD_TYPE type;
	int size;
};

struct AUDIO_DEC_EOS {
	struct AUDIO_INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	int EOSID;  /* ID associated with command */
};

struct AUDIO_DEC_NEW_FORMAT {
	struct AUDIO_INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	enum AUDIO_DEC_TYPE audioType;
	int privateInfo[8];  // note: privateinfo[6] is used for choosing decoder sync pts method
};

struct AUDIO_DEC_PTS_INFO {
	struct AUDIO_INBAND_CMD_PKT_HEADER header;
	unsigned int               wPtr;
	unsigned int               PTSH;
	unsigned int               PTSL;
};

struct RPC_TOAGENT_STOP_T {
	struct RPCRES_LONG retval;
	uint32_t         res;
	int        instanceID;
};

struct RPC_TOAGENT_DESTROY_T {
	struct RPCRES_LONG retval;
	uint32_t         res;
	int        instanceID;
};

struct RPC_GET_VOLUME_T {
	struct RPCRES_LONG res;
	uint32_t ret;
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS  param;
};

struct AUDIO_GENERAL_CONFIG {
	char interface_en;
	char channel_in;
	char count_down_rec_en;
	int count_down_rec_cyc;
};

struct AUDIO_SAMPLE_INFO {
	int sampling_rate;
	int PCM_bitnum;
};

struct AUDIO_ADC_CONFIG {
	struct AUDIO_GENERAL_CONFIG audioGeneralConfig;
	struct AUDIO_SAMPLE_INFO sampleInfo;
};

struct AUDIO_CONFIG_ADC {
	int instanceID;
	struct AUDIO_ADC_CONFIG adcConfig;
};

struct AUDIO_RPC_REFCLOCK {
	int instanceID;
	int pRefClockID;
	int pRefClock;
};

struct AUDIO_RPC_REFCLOCK_T {
	int instanceID;
	int pRefClockID;
	int pRefClock;
	struct RPCRES_LONG ret;
	uint32_t res;
};

struct AUDIO_RPC_FOCUS {
	int instanceID;
	int focusID;
};

struct AUDIO_RPC_FOCUS_T {
	int instanceID;
	int focusID;
	struct RPCRES_LONG ret;
	uint32_t res;
};

struct AUDIO_OUT_GENERAL_CONFIG {
	char interface_en;
	char channel_out;
	char count_down_play_en;
	int count_down_play_cyc;
};

struct AUDIO_OUT_CS_INFO {
	char non_pcm_valid;
	char non_pcm_format;
	int audio_format;
	char spdif_consumer_use;
	char copy_right;
	char pre_emphasis;
	char stereo_channel;
};

struct AUDIO_DAC_CONFIG {
	struct AUDIO_OUT_GENERAL_CONFIG audioGeneralConfig;
	struct AUDIO_SAMPLE_INFO sampleInfo;
};

struct AUDIO_OUT_SPDIF_CONFIG {
	struct AUDIO_OUT_GENERAL_CONFIG audioGeneralConfig;
	struct AUDIO_SAMPLE_INFO sampleInfo;
	struct AUDIO_OUT_CS_INFO out_cs_info;
};

struct AUDIO_CONFIG_DAC_I2S {
	int instanceID;
	struct AUDIO_DAC_CONFIG dacConfig;
};

struct AUDIO_CONFIG_DAC_I2S_T {
	int instanceID;
	struct AUDIO_DAC_CONFIG dacConfig;
	uint32_t res;
};

struct AUDIO_CONFIG_DAC_SPDIF {
	int instanceID;
	struct AUDIO_OUT_SPDIF_CONFIG spdifConfig;
};

struct AUDIO_CONFIG_DAC_SPDIF_T {
	int instanceID;
	struct AUDIO_OUT_SPDIF_CONFIG spdifConfig;
	uint32_t res;
};

struct AUDIO_RINGBUF_PTR {
	int base;
	int limit;
	int cp;
	int rp;
	int wp;
};

struct AUDIO_RINGBUF_PTR_64 {
	unsigned long base;
	unsigned long limit;
	unsigned long cp;
	unsigned long rp;
	unsigned long wp;
};

struct ALSA_MEM_INFO {
	phys_addr_t pPhy;
	unsigned int *pVirt;
	unsigned int size;
};

struct ALSA_LATENCY_INFO {
	unsigned int latency;
	unsigned int ptsL;
	unsigned int ptsH;
	unsigned int sum;     // latency + ptsL
	unsigned int decin_wp;
	unsigned int sync;
	unsigned int dec_in_delay;
	unsigned int dec_out_delay;
	unsigned int ao_delay;
	int rvd[8];
};

struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS {
	int instanceID;
	enum AUDIO_ENUM_AIO_PRIVAETINFO type;
	int argateInfo[16];
};

struct AUDIO_RPC_DEC_PRIVATEINFO_PARAMETERS {
	long instanceID;
	enum AUDIO_ENUM_DEC_PRIVAETINFO type;
	long privateInfo[16];
};

/* RTK sound card */
struct RTK_snd_card {
	struct snd_card *card;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int mixer_change;
	int capture_source[MIXER_ADDR_LAST+1][2];
	int capture_change;
	struct work_struct work_volume;
	struct mutex rpc_lock;
	struct snd_pcm *pcm;
	struct snd_compr *compr;
};

/* RTK PCM instance */
struct snd_card_RTK_pcm {
/******************************************************************************************************************
 ** CAN'T change the order of the following variables, bcz AO(on SCPU) will refer RINGBUFFER_HEADER decOutRing[8] *
 ******************************************************************************************************************/
	struct RINGBUFFER_HEADER decOutRing[8];    /* big endian, in DEC-AO path, share with DEC and AO */
	struct RTK_snd_card *card;
	struct snd_pcm_substream *substream;
	struct RINGBUFFER_HEADER decInbandRing;    /* big endian, in DEC-AO path, inband ring of DEC */
	struct RINGBUFFER_HEADER decInRing[1];     /* big endian, uncache, in DEC-AO path, inring of DEC */
	struct RINGBUFFER_HEADER decInRing_LE[1];  /* little endian, uncache, duplication of nHWInring */

	snd_pcm_uframes_t nHWPtr;           /* in DEC-AO path, rp of in_ring of DEC, (0,1, ..., ,runntime->buffer_size-1) */
	snd_pcm_uframes_t nPreHWPtr;
	snd_pcm_uframes_t nPre_appl_ptr;
	snd_pcm_uframes_t nHWReadSize;      /* less than runntime->period_size */
	snd_pcm_uframes_t nTotalRead;
	snd_pcm_uframes_t nTotalWrite;

#ifdef DEBUG_RECORD
	struct file *fp;
	loff_t pos;                         /* The param for record data send to alsa */
	mm_segment_t fs;
#endif

	struct hrtimer hr_timer;           /* Hr timer for playback */
	enum hrtimer_restart enHRTimer;    /* Hr timer state */
	ktime_t ktime;                     /* Ktime for hr timer */
	int PlayBackHandle;
	enum SND_REALTEK_EOS_STATE_T nEOSState;
	int DECAgentID;
	int DECpinID;
	int AOAgentID;
	int AOpinID;
	int volume;
	int bInitRing;
	int bHWinit;
	int last_channel;
	int ao_decode_lpcm;
	int dec_out_msec;
	unsigned int decInbandData[64];     //inband ring
	int *g_ShareMemPtr;
	struct ALSA_LATENCY_INFO *g_ShareMemPtr2;
	struct ALSA_LATENCY_INFO *g_ShareMemPtr3;
	unsigned int nPeriodBytes;
	unsigned int nRingSize;          // bytes
	unsigned int dbg_count;
	unsigned int audio_count;
	phys_addr_t phy_decOutData[8];   // physical address of Output data
	phys_addr_t phy_addr;            // physical address of struct snd_card_RTK_pcm;
	phys_addr_t phy_addr_rpc;    // physical address for doing suspend
	phys_addr_t g_ShareMemPtr_dat;   // physical address for latency
	phys_addr_t g_ShareMemPtr_dat2;
	phys_addr_t g_ShareMemPtr_dat3;
	struct RINGBUFFER_HEADER decOutRing_LE[8];    /* little endian, in DEC-AO path, share with DEC and AO */
	void *vaddr_decOutData[8];
	void *vaddr_rpc;  //virtual address for doing suspend
};

struct snd_card_RTK_capture_pcm {
	struct RTK_snd_card *card;
	struct snd_pcm_substream *substream;
	struct RINGBUFFER_HEADER nAIRing[8];       /* big endian */
	struct RINGBUFFER_HEADER nAIRing_LE[8];    /* little endian */
	struct RINGBUFFER_HEADER nLPCMRing;        /* big endian */
	struct RINGBUFFER_HEADER nLPCMRing_LE;     /* little endian */
	struct RINGBUFFER_HEADER nPTSRingHdr;      /* big endian */
	struct RINGBUFFER_HEADER decInRing[1];     /* big endian */
	struct RINGBUFFER_HEADER decInRing_LE[1];  /* little endian */
	snd_pcm_uframes_t nAIRingWp;
	snd_pcm_uframes_t nAIRingPreWp;
	snd_pcm_uframes_t nTotalWrite;
	enum AUDIO_FORMAT_OF_AI_SEND_TO_ALSA nAIFormat;

	struct hrtimer hr_timer;           /* Hr timer for playback */
	enum hrtimer_restart enHRTimer;    /* Hr timer state */
	ktime_t ktime;                     /* Ktime for hr timer */
	int CaptureHandle;
	int AIAgentID;
	int AOAgentID;
	int AOpinID;
	int bInitRing;
	int source_in;
	int dmic_volume[2];
	int *pAIRingData[8]; // kernel virtual address
	unsigned int *pLPCMData;
	unsigned int nPeriodBytes;
	unsigned int nFrameBytes;
	unsigned int nRingSize; // bytes
	unsigned int nLPCMRingSize;
	phys_addr_t phy_pAIRingData[8]; //physical address of ai data
	phys_addr_t phy_pLPCMData;      //physical address of lpcm data
	phys_addr_t phy_addr;           //physical addresss of struct snd_card_RTK_capture_pcm;
	phys_addr_t phy_addr_rpc;   //physical address for doing suspend
	struct ALSA_MEM_INFO nPTSMem;
	struct AUDIO_RINGBUF_PTR_64 nPTSRing;
	struct timespec64 ts;
	size_t size;
	size_t size_rpc;

	void *vaddr_rpc;  //virtual address for doing suspend
};

struct snd_pcm_mmap_fd {
	int32_t dir;
	int32_t fd;
	int32_t size;
	int32_t actual_size;
};

#define SNDRV_PCM_IOCTL_GET_LATENCY  _IOR('A', 0xF0, int)
#define SNDRV_PCM_IOCTL_GET_FW_DELAY _IOR('A', 0xF1, unsigned int)
#define SNDRV_PCM_IOCTL_MMAP_DATA_FD _IOWR('A', 0xE4, struct snd_pcm_mmap_fd)
#define SNDRV_PCM_IOCTL_DMIC_VOL_SET _IOW('A', 0xE6, int)
/************************************************************************/
/* PROTOTYPE                                                            */
/************************************************************************/
int snd_realtek_hw_ring_write(struct RINGBUFFER_HEADER *ring, void *data, int len, unsigned int offset);
int writeInbandCmd_afw(struct snd_card_RTK_pcm *dpcm, void *data, int len);
uint64_t snd_card_get_90k_pts(void);
int snd_afw_ept_init(struct rtk_krpc_ept_info *krpc_ept_info);

// RPC function
int RPC_TOAGENT_CHECK_AUDIO_READY(phys_addr_t paddr, void *vaddr);
int RPC_TOAGENT_CONNECT_SVC_AFW(phys_addr_t paddr, void *vaddr, struct AUDIO_RPC_CONNECTION *pconnection);
int RPC_TOAGENT_DESTROY_SVC_AFW(phys_addr_t paddr, void *vaddr, int instanceID);
int RPC_TOAGENT_FLUSH_SVC_AFW(phys_addr_t paddr, void *vaddr, struct AUDIO_RPC_SENDIO *sendio);
int RPC_TOAGENT_INBAND_EOS_SVC_AFW(struct snd_card_RTK_pcm *dpcm);
int RPC_TOAGENT_INITRINGBUFFER_HEADER_SVC_AFW(phys_addr_t paddr, void *vaddr, struct AUDIO_RPC_RINGBUFFER_HEADER *header, int buffer_count);
int RPC_TOAGENT_PAUSE_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id);
int RPC_TOAGENT_RUN_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id);
int RPC_TOAGENT_STOP_SVC_AFW(phys_addr_t paddr, void *vaddr, int instanceID);
int RPC_TOAGENT_CREATE_AO_AGENT_AFW(phys_addr_t paddr, void *vaddr, int *aoId, int pinId);
int RPC_TOAGENT_GET_AO_FLASH_PIN_AFW(phys_addr_t paddr, void *vaddr, int AOAgentID);
int RPC_TOAGENT_CREATE_DECODER_AGENT_AFW(phys_addr_t paddr, void *vaddr, int *decId, int *pinId);
int RPC_TOAGENT_SET_AO_FLASH_VOLUME_AFW(phys_addr_t paddr, void *vaddr,	struct snd_card_RTK_pcm *dpcm);
int RPC_TOAGENT_RELEASE_AO_FLASH_PIN_AFW(phys_addr_t paddr, void *vaddr, int AOAgentID, int AOpinID);
int RPC_TOAGENT_SET_VOLUME_AFW(struct device *dev, int volume);
int RPC_TOAGENT_GET_VOLUME_AFW(phys_addr_t paddr, void *vaddr);
int RPC_TOAGENT_PUT_SHARE_MEMORY_LATENCY_AFW(phys_addr_t paddr, void *vaddr, void *p, void *p2, int decID, int aoID, int type);
int RPC_TOAGENT_CREATE_AI_AGENT_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AI_CONNECT_ALSA_AFW(phys_addr_t paddr, void *vaddr, struct snd_pcm_runtime *runtime);
int RPC_TOAGENT_AI_DISCONNECT_ALSA_AUDIO_AFW(phys_addr_t paddr, void *vaddr, struct snd_pcm_runtime *runtime);
int RPC_TOAGENT_AI_CONFIG_HDMI_RX_IN_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AI_CONFIG_I2S_IN_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AI_CONFIG_AUDIO_IN_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AI_CONFIG_I2S_LOOPBACK_IN_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AI_CONFIG_DMIC_PASSTHROUGH_IN_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_DESTROY_AI_FLOW_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id);
int RPC_TOAGENT_SET_LOW_WATER_LEVEL(phys_addr_t paddr, void *vaddr, bool isLowWater);
int RPC_TOAGENT_GET_AI_AGENT_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_AO_CONFIG_WITHOUT_DECODER_AFW(phys_addr_t paddr, void *vaddr, struct snd_pcm_runtime *runtime);
int RPC_TOAGENT_CREATE_GLOBAL_AO_AFW(phys_addr_t paddr, void *vaddr, int *aoId);
int RPC_TOAGENT_AI_CONNECT_AO_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_capture_pcm *dpcm);
int RPC_TOAGENT_SET_MAX_LATENCY_AFW(phys_addr_t paddr, void *vaddr, struct snd_card_RTK_pcm *dpcm);

#endif //SND_REALTEK_H
