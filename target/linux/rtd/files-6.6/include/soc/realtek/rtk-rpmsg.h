/* SPDX-License-Identifier: GPL-2.0-only */

#define AUDIO_ID 0x1
#define VIDEO_ID 0x2
#define VE3_ID 0x3
#define HIFI_ID 0x4
#define REPLYID 99
#define KERNELID 98
#define RPC_TIMEOUT (5 * HZ)

#define REMOTE_ALLOC 0xffffffff

#define S_OK        0x10000000

struct rpc_struct {
	uint32_t programID;
	uint32_t versionID;
	uint32_t procedureID;
	uint32_t taskID;
	uint32_t sysTID;
	uint32_t sysPID;
	uint32_t parameterSize;
	uint32_t mycontext;
};

void rtk_dump_all_ringbuf_info(struct device *dev);

int rcpu_endian_check(struct device *dev);
void endian_swap_32_read(void *buf, size_t size);
void endian_swap_32_write(void *buf, size_t size);
