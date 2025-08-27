#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/dma-buf.h>
#include <linux/dma-map-ops.h>
#include <linux/delay.h>

#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>

#ifdef CONFIG_RPMSG_RTK_RPC
#include <soc/realtek/rtk-krpc-agent.h>
#else				/* CONFIG_REALTEK_RPC */
#include <soc/realtek/kernel-rpc.h>
#endif

#include <uapi/soc/realtek/npupp_uapi.h>

#include "npupp_rpc.h"
#include "npupp_inband.h"

#define RPC_BUFFER_SIZE 4096

#define WB_PINID 0x20140507
#define WBINBAND_VERSION 0x72746b30	// rtk0

#define DEFAULT_TRANSCODE_WB_BUFFERNUM 1

#define PLOCK_BUFFER_SET_SIZE 32
#define PLOCK_BUFFER_SET 2
#define PLOCK_MAX_BUFFER_INDEX (PLOCK_BUFFER_SET_SIZE * PLOCK_BUFFER_SET)
#define PLOCK_BUFFER_SIZE (PLOCK_MAX_BUFFER_INDEX * 2)

typedef unsigned int HRESULT;

struct RPCRES_LONG {
	HRESULT result;
	unsigned int data;
};
typedef struct RPCRES_LONG RPCRES_LONG;

struct VIDEO_RPC_INSTANCE {
	enum VIDEO_VF_TYPE type;
};
typedef struct VIDEO_RPC_INSTANCE VIDEO_RPC_INSTANCE;

struct video_rpc_create_t {
	VIDEO_RPC_INSTANCE instance;
	RPCRES_LONG retval;
	HRESULT ret;
};

struct video_rpc_destroy_t {
	u_int instanceID;
	RPCRES_LONG retval;
	HRESULT ret;
};

struct VIDEO_RPC_CONFIG_WRITEBACK_FLOW {
	u_int instanceID;
	ENUM_WRITEBACK_TYPE type;
	u_int reserved[10];
};
typedef struct VIDEO_RPC_CONFIG_WRITEBACK_FLOW VIDEO_RPC_CONFIG_WRITEBACK_FLOW;

struct video_rpc_config_writeback_flow_t {
	VIDEO_RPC_CONFIG_WRITEBACK_FLOW flow;
	RPCRES_LONG retval;
	HRESULT ret;
};

struct video_rpc_npp_init_t {
	RPCRES_LONG retval;
	HRESULT ret;
};

struct video_rpc_npp_destroy_t {
	u_int instanceID;
	RPCRES_LONG retval;
	HRESULT ret;
};

struct video_rpc_run_t {
	u_int instanceID;
	RPCRES_LONG retval;
	HRESULT ret;
};

struct video_rpc_ringbuffer_t {
	RPCRES_LONG retval;
	HRESULT ret;
	unsigned int instanceID;
	unsigned int pinID;
	unsigned int readPtrIndex;
	unsigned int pRINGBUFF_HEADER;
};

struct RPC_RINGBUFFER {
	unsigned int instanceID;
	unsigned int pinID;
	unsigned int readPtrIndex;
	unsigned int pRINGBUFF_HEADER;
};
typedef struct RPC_RINGBUFFER RPC_RINGBUFFER;

struct npp_rpc_info {
	struct device *dev;
	unsigned int ret;
	void *vaddr;
	dma_addr_t paddr;
#ifdef CONFIG_RPMSG_RTK_RPC
	struct rtk_krpc_ept_info *acpu_ept_info;
#endif
};

struct npp_rpc_info *rpc_info;

struct npp_ringbuffer_info {
	void *vaddr;
	dma_addr_t paddr;
	RINGBUFFER_HEADER *ringheader;
};

/* ICQ ringbuffer */
struct npp_ringbuffer_info *rb_icq;

/* Writeback ringbuffer */
struct npp_ringbuffer_info *rb_wb;

struct lock_info {
	void *vaddr;
	dma_addr_t paddr;
};

struct lock_info *lock_info, *recv_info;

enum {
	WRITEBACK_TYPE_BACKGROUND,
	WRITEBACK_TYPE_VSYNC_V1,
};

unsigned int instance_id;

struct rtk_npupp_device {
	struct device *dev;
	struct miscdevice mdev;

	int npupp_instance;
	struct mutex lock;
};

#ifdef CONFIG_RPMSG_RTK_RPC
static int krpc_acpu_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	uint32_t *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (uint32_t *) (buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) = *(tmp + 1);

		complete(&krpc_ept_info->ack);
	}

	return 0;
}

static int npupp_ept_init(struct rtk_krpc_ept_info *krpc_ept_info)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, "rtk-npupp", krpc_acpu_cb);

	return ret;
}

static int npupp_ept_deinit(struct rtk_krpc_ept_info *krpc_ept_info)
{
	krpc_info_deinit(rpc_info->acpu_ept_info);
	krpc_ept_info_put(rpc_info->acpu_ept_info);

	return 0;
}

static int npupp_krpc_init(void)
{
	struct device_node *np = rpc_info->dev->of_node;
	int ret;

	if (IS_ENABLED(CONFIG_RPMSG_RTK_RPC)) {
		rpc_info->acpu_ept_info = of_krpc_ept_info_get(np, 0);
		if (IS_ERR(rpc_info->acpu_ept_info)) {
			ret = PTR_ERR(rpc_info->acpu_ept_info);
			if (ret == -EPROBE_DEFER) {
				pr_err
				    ("rtk_npupp: krpc ept info not ready, retry\n");
			} else {
				pr_err
				    ("rtk_npupp: Failed to get krpc ept info: %d\n",
				     ret);
			}

			return ret;
		}

		npupp_ept_init(rpc_info->acpu_ept_info);
	}

	return ret;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info,
			      uint32_t command, uint32_t param1,
			      uint32_t param2, int *len)
{
	struct rpc_struct *rpc;
	uint32_t *tmp;
	char *buf;

	*len = sizeof(struct rpc_struct) + 3 * sizeof(uint32_t);
	buf = kmalloc(sizeof(struct rpc_struct) + 3 * sizeof(uint32_t),
		      GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rpc = (struct rpc_struct *)buf;
	rpc->programID = KERNELID;
	rpc->versionID = KERNELID;
	rpc->procedureID = 0;
	rpc->taskID = krpc_ept_info->id;
	rpc->sysTID = krpc_ept_info->id;
	rpc->sysPID = krpc_ept_info->id;
	rpc->parameterSize = 3 * sizeof(uint32_t);
	rpc->mycontext = 0;
	tmp = (uint32_t *) (buf + sizeof(struct rpc_struct));
	*tmp = command;
	*(tmp + 1) = param1;
	*(tmp + 2) = param2;

	return buf;
}

int npupp_send_rpc(struct device *dev, struct rtk_krpc_ept_info *krpc_ept_info,
		   char *buf, int len, uint32_t *retval)
{
	int ret = 0;

	mutex_lock(&krpc_ept_info->send_mutex);

	krpc_ept_info->retval = retval;
	ret = rtk_send_rpc(krpc_ept_info, buf, len);
	if (ret < 0) {
		pr_err("[%s] send rpc failed\n", krpc_ept_info->name);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return ret;
	}
	if (!wait_for_completion_timeout(&krpc_ept_info->ack, RPC_TIMEOUT)) {
		dev_err(dev, "kernel rpc timeout: %s...\n",
			krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}
#endif				/* CONFIG_RPMSG_RTK_RPC */

static int send_rpc(struct npp_rpc_info *rpc_info, int opt, uint32_t command,
		    uint32_t param1, uint32_t param2, uint32_t * retval)
{
	int ret = 0;

#ifdef CONFIG_RPMSG_RTK_RPC
	char *buf;
	int len;

	if (opt == RPC_AUDIO) {
		buf = prepare_rpc_data(rpc_info->acpu_ept_info, command,
				       param1, param2, &len);
		if (!IS_ERR(buf)) {
			ret = npupp_send_rpc(rpc_info->dev,
					     rpc_info->acpu_ept_info, buf, len,
					     retval);
			kfree(buf);
		}
	}
#else
	ret = send_rpc_command(opt, command, param1, param2, retval);
#endif

	return ret;
}

static int npupp_rpc_create(void)
{
	struct video_rpc_create_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instance.type = htonl(VF_TYPE_VIDEO_OUT);
	offset = get_rpc_alignment_offset(sizeof(rpc->instance));

	/* VIDEO_RPC_ToAgent_Create_0: ENUM_VIDEO_KERNEL_RPC_CREATE = 41 */
	command = ENUM_VIDEO_KERNEL_RPC_CREATE;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	instance_id = ntohl(rpc->retval.data);

	return 0;
}

static int npupp_rpc_destroy(void)
{
	struct video_rpc_destroy_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(instance_id);
	offset = get_rpc_alignment_offset(sizeof(rpc->instanceID));

	/* VIDEO_RPC_ToAgent_Destroy_0: ENUM_VIDEO_KERNEL_RPC_DESTROY = 55 */
	command = ENUM_VIDEO_KERNEL_RPC_DESTROY;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_npp_init(void)
{
	struct video_rpc_npp_init_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	/* Use no parameter */
	offset = 0;

	/* VIDEO_RPC_VOUT_NPP_Init_0: ENUM_VIDEO_KERNEL_RPC_NPP_Init = 78 */
	command = ENUM_VIDEO_KERNEL_RPC_NPP_Init;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_npp_destroy(void)
{
	struct video_rpc_npp_destroy_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(instance_id);
	offset = get_rpc_alignment_offset(sizeof(rpc->instanceID));

	/* VIDEO_RPC_VOUT_NPP_Destroy_0: ENUM_VIDEO_KERNEL_RPC_NPP_Destroy = 79 */
	command = ENUM_VIDEO_KERNEL_RPC_NPP_Destroy;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_config(int type)
{
	struct video_rpc_config_writeback_flow_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->flow.instanceID = htonl(instance_id);
	rpc->flow.type = htonl(type);
	offset = get_rpc_alignment_offset(sizeof(rpc->flow));

	/* VIDEO_RPC_ToAgent_ConfigWriteBackFlow_0: ENUM_VIDEO_KERNEL_RPC_ConfigWriteBackFlow = 77 */
	command = ENUM_VIDEO_KERNEL_RPC_ConfigWriteBackFlow;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_run(void)
{
	struct video_rpc_run_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(instance_id);
	offset = get_rpc_alignment_offset(sizeof(rpc->instanceID));

	/* VIDEO_RPC_ToAgent_Run_0: ENUM_VIDEO_KERNEL_RPC_RUN = 45 */
	command = ENUM_VIDEO_KERNEL_RPC_RUN;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_pause(void)
{
	struct video_rpc_run_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(instance_id);
	offset = get_rpc_alignment_offset(sizeof(rpc->instanceID));

	/* VIDEO_RPC_ToAgent_Pause_0: ENUM_VIDEO_KERNEL_RPC_PAUSE = 53 */
	command = ENUM_VIDEO_KERNEL_RPC_PAUSE;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_stop(void)
{
	struct video_rpc_run_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(instance_id);
	offset = get_rpc_alignment_offset(sizeof(rpc->instanceID));

	/* VIDEO_RPC_ToAgent_Stop_0: ENUM_VIDEO_KERNEL_RPC_STOP = 54 */
	command = ENUM_VIDEO_KERNEL_RPC_STOP;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr, rpc_info->paddr + offset, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_rpc_init_ringbuffer(RPC_RINGBUFFER * ringbuffer)
{
	struct video_rpc_ringbuffer_t *rpc;
	unsigned int offset;
	unsigned int command;

	rpc = rpc_info->vaddr;
	memset(rpc, 0, RPC_BUFFER_SIZE);

	rpc->instanceID = htonl(ringbuffer->instanceID);
	rpc->pinID = htonl(ringbuffer->pinID);
	rpc->readPtrIndex = htonl(ringbuffer->readPtrIndex);
	rpc->pRINGBUFF_HEADER = htonl(ringbuffer->pRINGBUFF_HEADER);
	offset =
	    get_rpc_alignment_offset(sizeof(rpc->retval) + sizeof(rpc->ret));

	/* VIDEO_RPC_ToAgent_InitRingBuffer_0: ENUM_VIDEO_KERNEL_RPC_INITRINGBUFFER = 46 */
	command = ENUM_VIDEO_KERNEL_RPC_INITRINGBUFFER;
	send_rpc(rpc_info, RPC_AUDIO, command,
		 rpc_info->paddr + offset, rpc_info->paddr, &rpc->ret);

	if ((rpc->ret != S_OK)
	    || (ntohl(rpc->retval.result) != S_OK)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	return 0;
}

static int npupp_get_dmaaddr(struct rtk_npupp_device *npupp_dev,
			     struct npupp_addr_info *addr_info)
{
	struct device *dev = npupp_dev->dev;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int ret = 0;

	dmabuf = dma_buf_get(addr_info->handle);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		pr_err("%s: cannot get dma_buf\n", __func__);
		goto get_err;
	}

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		pr_err("%s: cannot attach\n", __func__);
		goto put_dma_buf;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		pr_err("%s: cannot map attachment\n", __func__);
		goto detach_dma_buf;
	}

	dma_addr = sg_dma_address(sgt->sgl);
	if (!dma_addr) {
		ret = -ENOMEM;
		goto unmap_attachment;
	}

	addr_info->addr = dma_addr;

 unmap_attachment:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);

 detach_dma_buf:
	dma_buf_detach(dmabuf, attach);

 put_dma_buf:
	dma_buf_put(dmabuf);

 get_err:
	return ret;
}

static int npupp_init_ringbuffer(struct npp_ringbuffer_info *rb,
				 unsigned int size, unsigned int pinid)
{
	RPC_RINGBUFFER ringbuffer;
	int ret;

	rb->ringheader =
	    (RINGBUFFER_HEADER *) ((unsigned long)(rb->vaddr) + (size));
	rb->ringheader->beginAddr = htonl((long)(0xffffffff & rb->paddr));
	rb->ringheader->size = htonl(size);
	rb->ringheader->writePtr = rb->ringheader->beginAddr;
	rb->ringheader->readPtr[0] = rb->ringheader->beginAddr;
	rb->ringheader->bufferID = htonl(1);

	memset(&ringbuffer, 0, sizeof(ringbuffer));
	ringbuffer.instanceID = instance_id;
	ringbuffer.pinID = pinid;
	ringbuffer.readPtrIndex = 0;
	ringbuffer.pRINGBUFF_HEADER = (long)(0xffffffff & (rb->paddr)) + size;

	ret = npupp_rpc_init_ringbuffer(&ringbuffer);
	if (ret < 0)
		return ret;

	return 0;
}

int npupp_init(void)
{
	int ret;

	/* Create */
	ret = npupp_rpc_create();
	if (ret < 0)
		return ret;

	/* NPP Init */
	ret = npupp_rpc_npp_init();
	if (ret < 0)
		return ret;

	/* Init ICQ ringbuffer */
	ret = npupp_init_ringbuffer(rb_icq, 64 * 1024, 0);
	if (ret < 0)
		return ret;

	/* Init writeback ringbuffer */
	ret = npupp_init_ringbuffer(rb_wb, 64 * 1024, WB_PINID);
	if (ret < 0)
		return ret;

	return 0;
}

static int npupp_open(struct inode *inode, struct file *filp)
{
	struct rtk_npupp_device *npupp_dev =
	    container_of(filp->private_data, struct rtk_npupp_device, mdev);
	int ret;

	mutex_lock(&npupp_dev->lock);

	if (npupp_dev->npupp_instance < 1) {
		pr_err("%s: instance = %d failed\n", __func__,
		       npupp_dev->npupp_instance);
		mutex_unlock(&npupp_dev->lock);
		return -EPERM;
	}
	npupp_dev->npupp_instance--;

	mutex_unlock(&npupp_dev->lock);

#ifdef CONFIG_RPMSG_RTK_RPC
	ret = npupp_krpc_init();
	if (ret < 0) {
		return -EINVAL;
	}
#endif

	ret = npupp_init();
	if (ret < 0) {
		mutex_lock(&npupp_dev->lock);
		npupp_dev->npupp_instance++;
		mutex_unlock(&npupp_dev->lock);

		return -EINVAL;
	}

	filp->private_data = npupp_dev;

	return 0;
}

static int npupp_allocate_lock(struct rtk_npupp_device *npupp_dev)
{
	struct device *dev = npupp_dev->dev;

	lock_info = kzalloc(sizeof(*lock_info), GFP_KERNEL);
	lock_info->vaddr = dma_alloc_coherent(dev, PLOCK_MAX_BUFFER_INDEX,
					      &lock_info->paddr, GFP_KERNEL);

	recv_info = kzalloc(sizeof(*recv_info), GFP_KERNEL);
	recv_info->vaddr = dma_alloc_coherent(dev, PLOCK_MAX_BUFFER_INDEX,
					      &recv_info->paddr, GFP_KERNEL);

	return 0;
}

static int npupp_free_lock(struct rtk_npupp_device *npupp_dev)
{
	struct device *dev = npupp_dev->dev;

	dma_free_coherent(dev, PLOCK_MAX_BUFFER_INDEX,
			  lock_info->vaddr, lock_info->paddr);
	kfree(lock_info);

	dma_free_coherent(dev, PLOCK_MAX_BUFFER_INDEX,
			  recv_info->vaddr, recv_info->paddr);
	kfree(recv_info);

	return 0;
}

static void memcpy_inband_swap(uint8_t * des, uint8_t * src, unsigned int size)
{
	unsigned int *src_int32 = (unsigned int *)src;
	unsigned int *des_int32 = (unsigned int *)des;
	unsigned int i;

	for (i = 0; i < (size / sizeof(int)); i++)
		des_int32[i] = htonl(src_int32[i]);
}

static int write_inband_cmd(struct npp_ringbuffer_info *rb_info, uint8_t * buf,
			    int size)
{
	RINGBUFFER_HEADER *ringheader = rb_info->ringheader;
	unsigned int wp, rp, rbsize;
	uint8_t *wptr, *next, *limit;
	int size0, size1;

	wp = htonl(ringheader->writePtr);
	rp = htonl(ringheader->readPtr[0]);
	rbsize = htonl(ringheader->size);

	/* Check available space for write size */
	if ((rp > wp) && (rp - wp - 1 < size)) {
		return -EAGAIN;
	}

	if ((wp > rp) && (rp + rbsize - wp - 1 < size)) {
		return -EAGAIN;
	}

	/* Get address for write */
	wptr = rb_info->vaddr + (wp - rb_info->paddr);
	next = wptr + size;
	limit = rb_info->vaddr + rbsize;

	/* Write cmd buffer to ring buffer */
	if (next >= limit) {
		size0 = limit - wptr;
		size1 = size - size0;
		memcpy_inband_swap(wptr, buf, size0);
		memcpy_inband_swap(rb_info->vaddr, buf + size0, size1);

		next -= rbsize;
	} else {
		memcpy_inband_swap(wptr, buf, size);
	}

	ringheader->writePtr =
	    htonl(rb_info->paddr + (next - (uint8_t *) rb_info->vaddr));

	return 0;
}

static int npupp_add_wb_buffer(struct rtk_npupp_device *npupp_dev,
			       struct npupp_transcode_info *t_info)
{
	VIDEO_VO_NPP_WB_PICTURE_OBJECT object = { 0 };
	struct npupp_addr_info addr_info;
	int buffer_num, i;
	int ret;

	buffer_num = DEFAULT_TRANSCODE_WB_BUFFERNUM;

	for (i = 0; i < buffer_num; i++) {
		/* Use cmd type VIDEO_NPP_INBAND_CMD_TYPE_WRITEBACK_BUFFER = 101 */
		object.header.type = VIDEO_NPP_INBAND_CMD_TYPE_WRITEBACK_BUFFER;
		object.header.size = sizeof(object);

		object.bufferNum = buffer_num;
		object.bufferId = i;
		object.bufferSize = t_info->stride * t_info->height;

		addr_info.handle = t_info->dstfd;
		addr_info.addr = 0;
		if (npupp_get_dmaaddr(npupp_dev, &addr_info))
			return -1;
		object.addrR = addr_info.addr;
		object.addrG = object.addrR + object.bufferSize;
		object.addrB = object.addrG + object.bufferSize;

		object.pitch = t_info->stride;
		object.version = WBINBAND_VERSION;
		object.pLock = lock_info->paddr;
		object.pReceived = recv_info->paddr;
		object.targetFormat = t_info->format;
		object.width = t_info->width;
		object.height = t_info->height;

		ret = write_inband_cmd(rb_icq,
				       (uint8_t *) & object, sizeof(object));
		if (ret < 0) {
			pr_err("%s failed %d\n", __func__, ret);
			return -1;
		}
	}

	return 0;
}

static int npupp_send_src_buffer(struct rtk_npupp_device *npupp_dev,
				 struct npupp_transcode_info *t_info)
{
	VIDEO_VO_PICTURE_OBJECT_NEW object = { 0 };
	struct npupp_addr_info addr_info = { 0 };
	int ret;

	/* Use cmd type VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC = 39 */
	object.header.type = VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC;
	object.header.size = sizeof(object);

	object.version = WBINBAND_VERSION;
	object.mode = CONSECUTIVE_FRAME;

	addr_info.handle = t_info->srcfd;
	addr_info.addr = 0;
	if (npupp_get_dmaaddr(npupp_dev, &addr_info))
		return -1;
	object.Y_addr = addr_info.addr;

	if (t_info->srcfd_uv) {
		addr_info.handle = t_info->srcfd_uv;
		addr_info.addr = 0;
		if (npupp_get_dmaaddr(npupp_dev, &addr_info))
			return -1;
		object.U_addr = addr_info.addr;
	} else {
		object.U_addr =
		    object.Y_addr + t_info->src_width * t_info->src_height;
	}

	object.width = t_info->src_width;
	object.height = t_info->src_height;
	object.Y_pitch = t_info->src_stride;

	ret = write_inband_cmd(rb_icq, (uint8_t *) & object, sizeof(object));
	if (ret < 0) {
		pr_err("%s failed %d\n", __func__, ret);
		return -1;
	}

	return 0;
}

static int read_inband_cmd(struct npp_ringbuffer_info *rb_info, uint8_t * buf,
			   int size)
{
	RINGBUFFER_HEADER *ringheader = rb_info->ringheader;
	unsigned int wp, rp, rbsize;
	uint8_t *rptr, *next, *limit;
	int size0, size1;

	wp = htonl(ringheader->writePtr);
	rp = htonl(ringheader->readPtr[0]);
	rbsize = htonl(ringheader->size);

	/* Check available space for read size */
	if ((wp >= rp) && (wp - rp < size)) {
		return -EAGAIN;
	}

	if ((rp > wp) && (wp + rbsize - rp < size)) {
		return -EAGAIN;
	}

	/* Get address for read */
	rptr = rb_info->vaddr + (rp - rb_info->paddr);
	next = rptr + size;
	limit = rb_info->vaddr + rbsize;

	/* Read from ring buffer into cmd buffer */
	if (next > limit) {
		size0 = limit - rptr;
		size1 = size - size0;
		memcpy_inband_swap(buf, rptr, size0);
		memcpy_inband_swap(buf + size0, rb_info->vaddr, size1);

		next -= rbsize;
	} else {
		memcpy_inband_swap(buf, rptr, size);
	}

	ringheader->readPtr[0] =
	    htonl(rb_info->paddr + (next - (uint8_t *) rb_info->vaddr));

	return 0;
}

static int npupp_get_wb_frame(struct npp_ringbuffer_info *rb_info)
{
	NPP_PICTURE_OBJECT_TRANSCODE object = { 0 };
	int timeout = 20;
	int ret;

	do {
		ret = read_inband_cmd(rb_info,
				      (uint8_t *) & object, sizeof(object));
		if (ret == 0)
			break;

		/* Use usleep_range to sleep for ~usecs or small msecs */
		usleep_range(500, 1000);
	} while (--timeout);

	if (ret) {
		pr_err("%s failed %d\n", __func__, ret);
		return ret;
	}

	/* receive type VIDEO_NPP_OUT_INBAND_CMD_TYPE_OBJ_PIC = 102 (0x66) */
	if (object.header.type != VIDEO_NPP_OUT_INBAND_CMD_TYPE_OBJ_PIC) {
		return -1;
	}

	return 0;
}

static int npupp_ioctl_transcode(struct rtk_npupp_device *npupp_dev,
				 struct npupp_transcode_info *t_info)
{
	int type;
	int ret;

	/* Config */
	if (t_info->type == WRITEBACK_TYPE_BACKGROUND)
		type = BACKGROUND_NPU_PP;

	ret = npupp_rpc_config(type);
	if (ret < 0)
		return ret;

	/* Run */
	ret = npupp_rpc_run();
	if (ret < 0)
		return ret;

	ret = npupp_allocate_lock(npupp_dev);

	/* Add writeback buffer */
	ret = npupp_add_wb_buffer(npupp_dev, t_info);
	if (ret < 0)
		return ret;

	/* Send video source buffer */
	ret = npupp_send_src_buffer(npupp_dev, t_info);
	if (ret < 0)
		return ret;

	/* Get writeback frame */
	ret = npupp_get_wb_frame(rb_wb);
	if (ret < 0)
		return ret;

	npupp_free_lock(npupp_dev);

	/* Pause */
	ret = npupp_rpc_pause();
	if (ret < 0)
		return ret;

	/* Stop */
	ret = npupp_rpc_stop();
	if (ret < 0)
		return ret;

	return 0;
}

static int npupp_release(struct inode *inode, struct file *filp)
{
	struct rtk_npupp_device *npupp_dev = filp->private_data;
	int ret;

	/* NPP Destroy */
	ret = npupp_rpc_npp_destroy();
	if (ret < 0)
		return ret;

	/* Destroy */
	ret = npupp_rpc_destroy();
	if (ret < 0)
		return ret;

#ifdef CONFIG_RPMSG_RTK_RPC
	npupp_ept_deinit(rpc_info->acpu_ept_info);
#endif

	mutex_lock(&npupp_dev->lock);
	npupp_dev->npupp_instance++;
	mutex_unlock(&npupp_dev->lock);

	return 0;
}

static long npupp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct rtk_npupp_device *data = filp->private_data;

	switch (cmd) {
	case NPUPP_INIT:{
			pr_info("%s: init is unused\n", __func__);
			break;
		}
	case NPUPP_TRANSCODE:{
			struct npupp_transcode_info transcode_info;

			if (copy_from_user(&transcode_info, (void __user *)arg,
					   sizeof(transcode_info))) {
				return -EFAULT;

			}

			if (npupp_ioctl_transcode(data, &transcode_info)) {
				return -EFAULT;
			}

			if (copy_to_user((void __user *)arg, &transcode_info,
					 sizeof(transcode_info))) {
				return -EFAULT;
			}

			break;
		}
	case NPUPP_DESTROY:{
			pr_info("%s: destroy is unused\n", __func__);
			break;
		}
	case NPUPP_GET_DMA_ADDR:{
			struct npupp_addr_info addr_info;

			if (copy_from_user(&addr_info, (void __user *)arg,
					   sizeof(addr_info))) {
				return -EFAULT;
			}

			if (npupp_get_dmaaddr(data, &addr_info)) {
				pr_err("%s: npp_get_dmaaddr failed\n",
				       __func__);
				return -EFAULT;
			}

			if (copy_to_user((void __user *)arg, &addr_info,
					 sizeof(addr_info))) {
				return -EFAULT;
			}

			break;
		}
	default:
		pr_err("%s: Unknown cmd 0x%x\n", __func__, cmd);
		break;
	}

	return 0;
}

static const struct file_operations npupp_fops = {
	.owner = THIS_MODULE,
	.open = npupp_open,
	.unlocked_ioctl = npupp_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.release = npupp_release,
};

static int rtk_npupp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_npupp_device *npupp_dev = NULL;
	int ret;

	npupp_dev = devm_kzalloc(dev, sizeof(*npupp_dev), GFP_KERNEL);
	if (!npupp_dev)
		return -ENOMEM;

	npupp_dev->mdev.minor = MISC_DYNAMIC_MINOR;
	npupp_dev->mdev.name = "rtk_npupp";
	npupp_dev->mdev.fops = &npupp_fops;
	npupp_dev->mdev.parent = NULL;

	ret = misc_register(&npupp_dev->mdev);
	if (ret) {
		pr_err("rtk_npupp: Failed to register misc device.\n");
		return ret;
	}

	set_dma_ops(dev, &rheap_dma_ops);
	rheap_setup_dma_pools(dev, "rtk_audio_heap",
			      RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			      RTK_FLAG_ACPUACC, __func__);

	/* Allocate RPC buffer */
	rpc_info = kzalloc(sizeof(*rpc_info), GFP_KERNEL);
	rpc_info->vaddr = dma_alloc_coherent(dev, RPC_BUFFER_SIZE,
					     &rpc_info->paddr, GFP_KERNEL);
	if (!rpc_info->vaddr) {
		pr_err("rtk_npupp: Failed to allocate RPC buffer.\n");
		return -ENOMEM;
	}

	rpc_info->dev = dev;

	/* Allocate ICQ ringbuffer */
	rb_icq = kzalloc(sizeof(*rb_icq), GFP_KERNEL);
	rb_icq->vaddr = dma_alloc_coherent(dev, 65 * 1024,
					   &rb_icq->paddr, GFP_KERNEL);
	if (!rb_icq->vaddr) {
		pr_err("rtk_npupp: Failed to allocate ringbuffer.\n");
		return -ENOMEM;
	}

	/* Allocate writeback ringbuffer */
	rb_wb = kzalloc(sizeof(*rb_wb), GFP_KERNEL);
	rb_wb->vaddr = dma_alloc_coherent(dev, 65 * 1024,
					  &rb_wb->paddr, GFP_KERNEL);
	if (!rb_wb->vaddr) {
		pr_err("rtk_npupp: Failed to allocate ringbuffer.\n");
		return -ENOMEM;
	}

	npupp_dev->dev = dev;
	npupp_dev->npupp_instance = 1;
	mutex_init(&npupp_dev->lock);

	platform_set_drvdata(pdev, npupp_dev);

	return 0;
}

static int rtk_npupp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_npupp_device *npupp_dev = platform_get_drvdata(pdev);

	dma_free_coherent(dev, RPC_BUFFER_SIZE,
			  rpc_info->vaddr, rpc_info->paddr);
	kfree(rpc_info);

	dma_free_coherent(dev, 65 * 1024, rb_icq->vaddr, rb_icq->paddr);
	kfree(rb_icq);

	dma_free_coherent(dev, 65 * 1024, rb_wb->vaddr, rb_wb->paddr);
	kfree(rb_wb);

	platform_set_drvdata(pdev, NULL);

	mutex_destroy(&npupp_dev->lock);
	misc_deregister(&npupp_dev->mdev);

	return 0;
}

static struct of_device_id rtk_npupp_ids[] = {
	{.compatible = "realtek,npupp"},
	{},
};

MODULE_DEVICE_TABLE(of, rtk_npupp_ids);

static struct platform_driver rtk_npupp_driver = {
	.probe = rtk_npupp_probe,
	.remove = rtk_npupp_remove,
	.driver = {
		   .name = "RTK_NPUPP",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rtk_npupp_ids),
		   },
};

module_platform_driver(rtk_npupp_driver);

MODULE_LICENSE("GPL");
