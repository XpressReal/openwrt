// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Realtek DHC Kernel RPC driver
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-map-ops.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/export.h>
#include <linux/miscdevice.h>
#include <linux/dma-map-ops.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <soc/realtek/rtk-rpmsg.h>

#define REMOTE_INIT BIT(0)
#define REMOTE_SUSPEND_READY BIT(1)
#define REMOTE_SUSPEND BIT(2)


#define FW_ALLOC_SPEC_MASK  0xC0000000
#define FW_ALLOC_VCPU_FWACC 0x40000000
#define FW_ALLOC_VCPU_EXTRA 0x80000000

extern struct raw_notifier_head rtk_rpmsg_chain_head;
unsigned int retry_count_value = 5;
unsigned int retry_delay_value = 200;
LIST_HEAD(client_list);

struct rtk_rpc_client {
	struct rpmsg_endpoint *ept;
	struct device *dev;
	char *name;
	int big_endian;
	struct rpmsg_device *rpdev;
	struct r_program_entry *r_program_head;
	int r_program_count;
	struct mutex r_program_lock;
	struct task_struct *r_program_kthread;
	wait_queue_head_t r_program_waitQueue;
	int r_program_flag;
	struct work_struct work;

	spinlock_t queue_lock;
	struct sk_buff_head queue;
	int (*send)(struct rtk_rpc_client *client, uint32_t command, uint32_t param1,
		uint32_t param2, uint32_t *ret);
	uint32_t *retval;
	struct completion ack;

	struct mutex send_lock;
	struct list_head list;
};


enum E_FW_ALLOC_FLAGS {
	eAlloc_Flag_SCPUACC                 = 1U << 31,
	eAlloc_Flag_ACPUACC                 = 1U << 30,
	eAlloc_Flag_HWIPACC                 = 1U << 29,
	eAlloc_Flag_VE_SPEC                 = 1U << 28,
	eAlloc_Flag_PROTECTED_AUDIO_POOL    = 1U << 27,
	eAlloc_Flag_PROTECTED_TP_POOL       = 1U << 26,
	eAlloc_Flag_PROTECTED_VO_POOL       = 1U << 25,
	eAlloc_Flag_PROTECTED_VIDEO_POOL    = 1U << 24,
	eAlloc_Flag_PROTECTED_AO_POOL       = 1U << 23,
	eAlloc_Flag_PROTECTED_METADATA_POOL = 1U << 22,
	eAlloc_Flag_VCPU_FWACC              = 1U << 21,
	eAlloc_Flag_CMA                     = 1U << 20,
	eAlloc_Flag_PROTECTED_FW_STACK      = 1U << 19,
	eAlloc_Flag_PROTECTED_EXT_BIT0      = 1U << 18,
	eAlloc_Flag_PROTECTED_EXT_BIT1      = 1U << 17,
	eAlloc_Flag_PROTECTED_EXT_BIT2      = 1U << 16,
	eAlloc_Flag_SKIP_ZERO               = 1U << 15,
};

struct fw_alloc_parameter {
	uint32_t size;
	uint32_t flags; /* enum E_FW_ALLOC_FLAGS */
} __attribute__((aligned(1)));

struct fw_alloc_parameter_legacy {
	uint32_t size;
} __attribute__((aligned(1)));


enum rpc_remote_cmd {
	RPC_REMOTE_CMD_ALLOC = 1,
	RPC_REMOTE_CMD_FREE = 2,
	RPC_REMOTE_CMD_ALLOC_SECURE = 3,
};

#define FW_ALLOC_SPEC_MASK  0xC0000000
#define FW_ALLOC_VCPU_FWACC 0x40000000
#define FW_ALLOC_VCPU_EXTRA 0x80000000

struct r_program_entry {
	struct device *dev;
	unsigned long phys_addr;
	void *addr;
	size_t size;
	struct dma_buf *dmabuf;
	struct r_program_entry *next;
	unsigned int flag;
};

struct rpc_mem_dma_buf_attachment {
	struct sg_table sgt;
};

struct rpc_mem_fd_data {
	unsigned long phyAddr;
	unsigned long ret_offset;
	unsigned long ret_size;
	int ret_fd;
};

#define RPC_MEM_IOC_MAGIC		'R'
#define RPC_MEM_IOC_EXPORT		_IOWR(RPC_MEM_IOC_MAGIC, 0, struct rpc_mem_fd_data)

static void r_program_add(struct rtk_rpc_client *client, struct r_program_entry *entry)
{
	mutex_lock(&client->r_program_lock);
	entry->next = client->r_program_head;
	client->r_program_head = entry;
	client->r_program_count++;
	mutex_unlock(&client->r_program_lock);
}

static struct r_program_entry *r_program_remove(struct rtk_rpc_client *client, unsigned long phys_addr)
{
	struct r_program_entry *prev = NULL;
	struct r_program_entry *curr = NULL;

	mutex_lock(&client->r_program_lock);
	curr = client->r_program_head;
	while (curr != NULL) {
		if (curr->phys_addr != phys_addr) {
			prev = curr;
			curr = curr->next;
			continue;
		}

		if (prev == NULL)
			client->r_program_head = curr->next;
		else
			prev->next = curr->next;

		client->r_program_count--;
		mutex_unlock(&client->r_program_lock);

		return curr;
	}
	mutex_unlock(&client->r_program_lock);
	return NULL;
}


static void rtk_rpc_free_ion(struct r_program_entry *rpc_entry)
{
	struct device *dev = rpc_entry->dev;

	dev_dbg(dev, "[%s] free addr : 0x%lx(vaddr=0x%lx) \n", __func__,
		(long unsigned int)rpc_entry->phys_addr, (long unsigned int)rpc_entry->addr);

	dma_free_coherent(dev, rpc_entry->size,
					rpc_entry->addr, rpc_entry->phys_addr);
}

static int rpc_mem_dma_buf_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attach)
{
	struct rpc_mem_dma_buf_attachment *a;
	struct r_program_entry *curr = dmabuf->priv;
	struct device *dev = curr->dev;

	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dma_get_sgtable(dev, &a->sgt, curr->addr, curr->phys_addr,
				 curr->size);
	if (ret < 0) {
		dev_err(dev, "failed to get scatterlist from DMA API\n");
		kfree(a);
		return -EINVAL;
	}

	attach->priv = a;

	return 0;
}

static void rpc_mem_dma_buf_detatch(struct dma_buf *dmabuf,
				    struct dma_buf_attachment *attach)
{
	struct rpc_mem_dma_buf_attachment *a = attach->priv;

	sg_free_table(&a->sgt);
	kfree(a);
}


static struct sg_table *rpc_mem_map_dma_buf(struct dma_buf_attachment *attach,
				enum dma_data_direction dir)
{
	struct rpc_mem_dma_buf_attachment *a = attach->priv;
	struct dma_buf *dmabuf = attach->dmabuf;
	struct r_program_entry *curr = dmabuf->priv;
	struct sg_table *table;
	int ret;

	table = &a->sgt;

	if (!(curr->flag & RTK_FLAG_SCPUACC) ||
		(curr->flag & RTK_FLAG_PROTECTED_MASK)) {
		if (!dma_map_sg_attrs(attach->dev, table->sgl, table->nents,
				dir, DMA_ATTR_SKIP_CPU_SYNC))
			table = ERR_PTR(-ENOMEM);
		return table;
	}

	ret = dma_map_sgtable(attach->dev, table, dir, 0);

	if (ret)
		table = ERR_PTR(ret);
	return table;
}

static void rpc_mem_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	struct dma_buf *dmabuf = attach->dmabuf;
	struct r_program_entry *curr = dmabuf->priv;

	if (!(curr->flag & RTK_FLAG_SCPUACC) ||
			(curr->flag & RTK_FLAG_PROTECTED_MASK) )
		dma_unmap_sg_attrs(attach->dev, table->sgl, table->nents,
				dir, DMA_ATTR_SKIP_CPU_SYNC);
	else
		dma_unmap_sg(attach->dev, table->sgl, table->nents,
				dir);
}


static int rpc_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct r_program_entry *curr = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	struct device *dev = curr->dev;
	dma_addr_t daddr = curr->phys_addr;
	void *vaddr = curr->addr;

	dev_dbg(dev, "%s \n", __func__);
	return dma_mmap_coherent(dev, vma, vaddr, daddr, size);
}

static void rpc_mem_release(struct dma_buf *dmabuf)
{

}

static const struct dma_buf_ops rpc_dma_buf_ops = {
	.attach = rpc_mem_dma_buf_attach,
	.detach = rpc_mem_dma_buf_detatch,
	.map_dma_buf = rpc_mem_map_dma_buf,
	.unmap_dma_buf = rpc_mem_unmap_dma_buf,
	.mmap = rpc_mem_mmap,
	.release = rpc_mem_release,
};


int r_program_fd(unsigned long phys_addr, unsigned long *offset,
		 unsigned long *size)
{
	int ret_fd = -1;
	struct r_program_entry *curr;
	struct list_head *plist;
	struct rtk_rpc_client *client;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	list_for_each(plist, &client_list){
		client = list_entry(plist, struct rtk_rpc_client, list);
		curr = client->r_program_head;
		mutex_lock(&client->r_program_lock);

		while (curr != NULL) {
			if (phys_addr >= curr->phys_addr &&
			    phys_addr < (curr->phys_addr + curr->size)) {

				exp_info.ops = &rpc_dma_buf_ops;
				exp_info.size = curr->size;
				exp_info.flags = O_RDWR;
				exp_info.priv = curr;
				curr->dmabuf = dma_buf_export(&exp_info);
				if (IS_ERR(curr->dmabuf)) {
					ret_fd = PTR_ERR(curr->dmabuf);
					mutex_unlock(&client->r_program_lock);
					goto out;
				}

				ret_fd = dma_buf_fd(curr->dmabuf, O_CLOEXEC);

				if (offset)
					*offset = phys_addr - curr->phys_addr;

				if (size)
					*size = curr->size;

				break;
			} else {
				curr = curr->next;
			}
		}
		mutex_unlock(&client->r_program_lock);
	}
out:
	return ret_fd;
}


struct compat_rpc_mem_fd_data {
	compat_ulong_t phyAddr;
	compat_ulong_t ret_offset;
	compat_ulong_t ret_size;
	compat_int_t ret_fd;
};


#ifdef CONFIG_COMPAT

#define COMPAT_RPC_MEM_IOC_EXPORT		_IOWR(RPC_MEM_IOC_MAGIC, 0, struct compat_rpc_mem_fd_data)

long compat_rpc_mem_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{

	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_RPC_MEM_IOC_EXPORT:
	{
		struct compat_rpc_mem_fd_data data32;
		struct rpc_mem_fd_data data;

		if (copy_from_user(&data32, compat_ptr(arg), sizeof(data32)))
			return -EFAULT;

		data = (struct rpc_mem_fd_data) {
			.phyAddr    = data32.phyAddr,
			.ret_offset = data32.ret_offset,
			.ret_size   = data32.ret_size,
			.ret_fd     = data32.ret_fd,
		};

		data.ret_fd = r_program_fd(data.phyAddr, &data.ret_offset, &data.ret_size);
		if (data.ret_fd < 0) {
			pr_err("%s : ret_fd = %d\n", __func__, data.ret_fd);
			return -EFAULT;
		}

		data32.phyAddr = data.phyAddr;
		data32.ret_offset = data.ret_offset;
		data32.ret_size = data.ret_size;
		data32.ret_fd = data.ret_fd;

		if (copy_to_user(compat_ptr(arg), &data32, sizeof(data32)))
			return -EFAULT;

		return 0;
	}

	default:
	{
		printk(KERN_ERR "[COMPAT_RPC_MEM] No such IOCTL, cmd is %d\n", cmd);
		return -ENOIOCTLCMD;
	}
	}
}
#endif

static long rpc_mem_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	long ret = -ENOTTY;
	struct rpc_mem_fd_data data;

	switch (cmd) {
	case RPC_MEM_IOC_EXPORT:
		if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
			pr_err("%s: copy_from_user ERROR!\n", __func__);
			break;
		}

		data.ret_fd = r_program_fd(data.phyAddr, &data.ret_offset,
					 &data.ret_size);
		if (data.ret_fd < 0) {
			pr_err("%s : ret_fd = %d\n", __func__, data.ret_fd);
				break;
		}
		if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		/*
>------->------- * The usercopy failed, but we can't do much about it, as
>------->------- * dma_buf_fd() already called fd_install() and made the
>------->------- * file descriptor accessible for the current process. It
>------->------- * might already be closed and dmabuf no longer valid when
>------->------- * we reach this point. Therefore "leak" the fd and rely on
>------->------- * the process exit path to do any required cleanup.
>------->------- */
			pr_err("%s : copy_to_user failed! (phyAddr=0x%08lx)\n",
				     __func__, data.phyAddr);
			break;
		}
		ret = 0;
		break;
	default:
		pr_err("%s: Unknown ioctl (cmd=0x%08x)\n", __func__, cmd);
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static const struct file_operations rpc_mem_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rpc_mem_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_rpc_mem_ioctl,
#endif
};

int rtk_rpc_mem_init(void)
{
	struct miscdevice *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->minor = MISC_DYNAMIC_MINOR;
	dev->name = "rpc_mem";
	dev->fops = &rpc_mem_fops;
	dev->parent = NULL;

	ret = misc_register(dev);
	if (ret) {
		pr_err("rpc_mem: failed to register misc device.\n");
		kfree(dev);
		return ret;
	}

	return 0;
}



void remote_alloc_reply(struct rtk_rpc_client *client, struct rpc_struct *rpc, unsigned long reply)
{
	char *buf = kmalloc(sizeof(struct rpc_struct) + (2 * sizeof(uint32_t)), GFP_KERNEL);
	struct rpc_struct *rrpc;
	uint32_t *tmp;
	int ret;

	rrpc = (struct rpc_struct *)buf;
	rrpc->programID = REPLYID;
	rrpc->versionID = REPLYID;
	rrpc->procedureID = 0;
	rrpc->taskID = 0;
	rrpc->sysTID = 0;
	rrpc->sysPID = 0;
	rrpc->parameterSize = 2 * sizeof(uint32_t);
	rrpc->mycontext = rpc->mycontext;

	tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
	*tmp = rpc->taskID;
	*(tmp + 1) = reply;

	if (client->big_endian)
		endian_swap_32_write((void *)buf, sizeof(struct rpc_struct) + (2 * sizeof(uint32_t)));


	ret = rpmsg_send(client->rpdev->ept, (void *)buf, sizeof(struct rpc_struct) + (2 * sizeof(uint32_t)));
	if (ret != sizeof(struct rpc_struct) + (2 * sizeof(uint32_t)))
		dev_err(client->dev, "send_rpc length error:%x %lx\n", ret, sizeof(struct rpc_struct) + (2 * sizeof(uint32_t)));

	kfree(buf);

}

unsigned int rpc_ion_alloc_handler_legacy(struct rtk_rpc_client *client, bool secure, const struct fw_alloc_parameter_legacy *param)
{
	unsigned int reply_value = 0;
	struct r_program_entry *rpc_entry;
	unsigned int fw_send_value = param->size;
	size_t alloc_val = 0;
	unsigned int alloc_flags;
	dma_addr_t daddr;
	void *vaddr;
	struct device *dev = client->dev;

	if (secure) {
		alloc_flags = RTK_FLAG_PROTECTED_V2_VO_POOL | RTK_FLAG_HWIPACC;
		if (!strcmp(client->name, "acpu"))
			alloc_flags |= RTK_FLAG_ACPUACC;
	} else {
		alloc_flags = RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
					 RTK_FLAG_HWIPACC | RTK_FLAG_ACPUACC;
	}

	alloc_val = PAGE_ALIGN(fw_send_value & ~FW_ALLOC_SPEC_MASK);

	if (fw_send_value & FW_ALLOC_VCPU_FWACC)
		alloc_flags |= RTK_FLAG_VCPU_FWACC;

	if (fw_send_value & FW_ALLOC_VCPU_EXTRA)
		alloc_flags |= RTK_FLAG_CMA;

	rheap_setup_dma_pools(dev, "rtk_media_heap", alloc_flags, client->name);
	vaddr = dma_alloc_coherent(dev, alloc_val, &daddr, GFP_KERNEL);

	if (!vaddr)
		goto rheap_err;

	rpc_entry = kmalloc(sizeof(struct r_program_entry),
			GFP_KERNEL);

	rpc_entry->next = NULL;
	rpc_entry->dev = dev;
	rpc_entry->addr = vaddr;
	rpc_entry->phys_addr = daddr;
	rpc_entry->size = alloc_val;
	r_program_add(client, rpc_entry);

	reply_value = rpc_entry->phys_addr;

	dev_dbg(dev, "[%s] alloc addr : 0x%x(vaddr 0x%lx) flags:0x%x\n",
		 __func__, reply_value, (long unsigned int)vaddr, alloc_flags);

	return reply_value;


rheap_err:
	reply_value = -1U;
	return reply_value;

}

unsigned int rpc_ion_alloc_handler(struct rtk_rpc_client *client, const struct fw_alloc_parameter *param)
{
	struct device *dev = client->dev;
	struct r_program_entry *rpc_entry;
	unsigned int reply_value = 0;
	size_t alloc_val = param->size;
	unsigned int alloc_flags = param->flags;
	unsigned int ion_alloc_flags = 0;
	dma_addr_t daddr;
	void *vaddr;

	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_SCPUACC                 )
				 ? RTK_FLAG_SCPUACC : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_ACPUACC                 )
				 ? RTK_FLAG_ACPUACC : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_HWIPACC                 )
				 ? RTK_FLAG_HWIPACC : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_VE_SPEC                 )
				 ? RTK_FLAG_VE_SPEC : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_AUDIO_POOL    )
				 ? RTK_FLAG_PROTECTED_V2_AUDIO_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_TP_POOL       )
				 ? RTK_FLAG_PROTECTED_V2_TP_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_VO_POOL       )
				 ? RTK_FLAG_PROTECTED_V2_VO_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_VIDEO_POOL    )
				 ? RTK_FLAG_PROTECTED_V2_VIDEO_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_AO_POOL       )
				 ? RTK_FLAG_PROTECTED_V2_AO_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_METADATA_POOL )
				 ? RTK_FLAG_PROTECTED_V2_METADATA_POOL : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_VCPU_FWACC              )
				 ? RTK_FLAG_VCPU_FWACC : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_CMA                     )
				 ? RTK_FLAG_CMA : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_FW_STACK      )
				 ? RTK_FLAG_PROTECTED_V2_FW_STACK : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_EXT_BIT0      )
				 ? RTK_FLAG_PROTECTED_EXT_BIT0 : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_EXT_BIT1      )
				 ? RTK_FLAG_PROTECTED_EXT_BIT1 : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_PROTECTED_EXT_BIT2      )
				 ? RTK_FLAG_PROTECTED_EXT_BIT2 : 0;
	ion_alloc_flags |= (alloc_flags & eAlloc_Flag_SKIP_ZERO               )
				 ? RTK_FLAG_SKIP_ZERO : 0;
	pr_info("%s alloc_size=0x%lx  ion_alloc_flags=0x%x \n", __func__,
				alloc_val, ion_alloc_flags);

	rheap_setup_dma_pools(dev, "rtk_media_heap", ion_alloc_flags, client->name);
	vaddr = dma_alloc_attrs(dev, alloc_val, &daddr, GFP_KERNEL,
						 DMA_ATTR_NO_KERNEL_MAPPING);
	if (!vaddr)
		goto rheap_err;

	rpc_entry = kmalloc(sizeof(struct r_program_entry), GFP_KERNEL);
	rpc_entry->next = NULL;
	rpc_entry->dev = dev;
	rpc_entry->addr = vaddr;
	rpc_entry->phys_addr = daddr;
	rpc_entry->size = alloc_val;
	rpc_entry->flag = ion_alloc_flags;
	r_program_add(client, rpc_entry);
	reply_value = rpc_entry->phys_addr;

	dev_dbg(dev, "[%s] alloc addr : 0x%lx(vaddr=0x%lx) (flags:0x%x)\n",
			 __func__, (long unsigned int)reply_value, (long unsigned int)vaddr, ion_alloc_flags);
	return reply_value;

rheap_err:
	reply_value = -1U;
	return reply_value;


}


static void remote_allocate_handler(struct rtk_rpc_client *client, char *buf)
{
	char *tmp;
	enum rpc_remote_cmd remote_cmd;
	unsigned long reply_value = 0;
	struct r_program_entry *rpc_entry;
	phys_addr_t phys_addr;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	remote_cmd = (enum rpc_remote_cmd) rpc->procedureID;
	tmp = (char *)(buf + sizeof(struct rpc_struct));

	switch (remote_cmd) {
	case RPC_REMOTE_CMD_ALLOC:
	case RPC_REMOTE_CMD_ALLOC_SECURE:

		if (rpc->parameterSize == sizeof(struct fw_alloc_parameter)) {
			struct fw_alloc_parameter param;

			memcpy((char *)&param, tmp, rpc->parameterSize);

			reply_value = rpc_ion_alloc_handler(client, &param);
		} else if (rpc->parameterSize == sizeof(struct fw_alloc_parameter_legacy)) {
			struct fw_alloc_parameter_legacy param;

			bool secure = (remote_cmd == RPC_REMOTE_CMD_ALLOC_SECURE) ? true : false;

			memcpy((char *)&param, tmp, rpc->parameterSize);

			reply_value = rpc_ion_alloc_handler_legacy(client, secure, &param);
		} else {
			dev_err(client->dev, "%s :RPC_REMOTE_CMD_ALLOC parameterSize(%d) mismatch!\n", __func__, rpc->parameterSize);
			break;
		}

		rpc->mycontext &= 0xfffffffc;

		break;
	case RPC_REMOTE_CMD_FREE:
		phys_addr = *(u32 *)tmp;
		rpc_entry = r_program_remove(client, phys_addr);

		if (rpc_entry) {
			rtk_rpc_free_ion(rpc_entry);
			kfree(rpc_entry);
			dev_dbg(client->dev, "[%s] ion_free addr : 0x%lx (reply_value : 0x%lx)\n", __func__, (long unsigned int)phys_addr, reply_value);
			reply_value = 0;
		} else {
			dev_err(client->dev, "[%s]cannot find rpc_entry to free:phys_addr:%lx\n", __func__, (long unsigned int)phys_addr);
			reply_value = -1U;
		}
		break;
	default:
		dev_err(client->dev, "[%s][%s]command not find %d\n", __func__, client->name, remote_cmd);
		break;
	}

	rpc->mycontext = rpc->mycontext & 0xfffffffc;

	remote_alloc_reply(client, rpc, reply_value);
}


static void  kern_rpc_work(struct work_struct *work)
{
	struct rtk_rpc_client *client = container_of(work, struct rtk_rpc_client, work);
	struct sk_buff *skb;
	struct rpc_struct *rpc;

	spin_lock(&client->queue_lock);
	if (skb_queue_empty(&client->queue))
		return;
	skb = skb_dequeue(&client->queue);
	spin_unlock(&client->queue_lock);

	rpc = (struct rpc_struct *)skb->data;

	dev_dbg(client->dev, "[%s]rpc->programID:%d, rpc->versionID:%d, rpc->procedureID:%d, rpc->taskID:%d, rpc->sysTID:%d, rpc->sysPID:%d, rpc->parameterSize:%d, rpc->mycontext:0x%x\n",
		__func__, rpc->programID, rpc->versionID, rpc->procedureID, rpc->taskID, rpc->sysTID, rpc->sysPID, rpc->parameterSize, rpc->mycontext);

	if (rpc->programID == 98)
		remote_allocate_handler(client, skb->data);
	/*else if (rpc->programID == 99)
		reply_handler(client, skb->data);
	*/
	kfree_skb(skb);
	if (!skb_queue_empty(&client->queue))
		schedule_work(&client->work);

}


static int rtk_rpc_callback(struct rpmsg_device *rpdev,
				void *data,
				int count,
				void *priv,
				u32 addr)
{
	struct rtk_rpc_client *client = dev_get_drvdata(&rpdev->dev);
	char *buf = (char *)data;
	struct sk_buff *skb;
	struct rpc_struct *rpc;

	rpc = (struct rpc_struct *)buf;

	if (client->big_endian)
		endian_swap_32_read(buf, sizeof(struct rpc_struct) + ntohl(rpc->parameterSize));

	dev_dbg(client->dev, "[%s]:rpc->programID:%d, rpc->versionID:%d, rpc->procedureID:%d, rpc->taskID:%d, rpc->sysTID:%d, rpc->sysPID:%d, rpc->parameterSize:%d, rpc->mycontext:0x%x\n",
				__func__, rpc->programID, rpc->versionID, rpc->procedureID, rpc->taskID, rpc->sysTID, rpc->sysPID, rpc->parameterSize, rpc->mycontext);

	skb = alloc_skb(count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, count);

	spin_lock(&client->queue_lock);
	skb_queue_tail(&client->queue, skb);
	spin_unlock(&client->queue_lock);

	schedule_work(&client->work);

	return 0;
}


static int rtk_rpc_probe(struct rpmsg_device *rpdev)
{
	struct rtk_rpc_client *client;
	int *flag;

	flag = devm_kzalloc(&rpdev->dev, sizeof(int), GFP_KERNEL);
	*flag = REMOTE_ALLOC;
	rpdev->ept->priv = (void *)flag;

	client = devm_kzalloc(&rpdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->dev = &rpdev->dev;
	client->ept = rpdev->ept;
	client->name = (char *)of_device_get_match_data(&rpdev->dev);

	client->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	client->dev->dma_mask = (u64 *)&client->dev->coherent_dma_mask;
	set_dma_ops(client->dev, &rheap_dma_ops);
	client->big_endian = rcpu_endian_check(client->dev->parent);

	client->rpdev = rpdev;
	mutex_init(&client->r_program_lock);
	mutex_init(&client->send_lock);
	spin_lock_init(&client->queue_lock);
	skb_queue_head_init(&client->queue);
	init_completion(&client->ack);
	dev_set_drvdata(&rpdev->dev, client);
	INIT_WORK(&client->work, kern_rpc_work);

	//if (strstr(client->name, "intr"))
	raw_notifier_call_chain(&rtk_rpmsg_chain_head, REMOTE_INIT, (void *)client->ept);

	list_add_tail(&client->list, &client_list);

	//rpmsg_set_signals(client->ept, REMOTE_INIT, 0);

	dev_dbg(&rpdev->dev, "probe\n");

	return of_platform_populate(rpdev->dev.of_node, NULL, NULL, &rpdev->dev);
}

static void rtk_rpc_remove(struct rpmsg_device *rpdev)
{
	of_platform_depopulate(&rpdev->dev);
}



static const struct of_device_id rtk_rpc_of_match[] = {
	//{ .compatible = "realtek,rpc-kernel-acpu", .data = "acpu" },
	//{ .compatible = "realtek,rpc-kernel-vcpu", .data = "vcpu" },
	//{ .compatible = "realtek,rpc-kernel-ve3", .data = "ve3" },
	//{ .compatible = "realtek,rpc-kernel-hifi", .data = "hifi" },
	{ .compatible = "realtek,rpc-intr-acpu", .data = "acpu-intr" },
	{ .compatible = "realtek,rpc-intr-vcpu", .data = "vcpu-intr" },
	{ .compatible = "realtek,rpc-intr-ve3", .data = "ve3-intr" },
	{ .compatible = "realtek,rpc-intr-hifi", .data = "hifi-intr" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_rpc_of_match);

static struct rpmsg_driver rtk_rpc_driver = {
	.probe = rtk_rpc_probe,
	.remove = rtk_rpc_remove,
	.callback = rtk_rpc_callback,
	.drv  = {
		.name  = "rtk_rpc",
		.of_match_table = rtk_rpc_of_match,
	},
};


static int __init rtk_rpc_init(void)
{
	rtk_rpc_mem_init();
	return register_rpmsg_driver(&rtk_rpc_driver);
}
module_init(rtk_rpc_init);

static void __exit rtk_rpc_exit(void)
{
	unregister_rpmsg_driver(&rtk_rpc_driver);
}
module_exit(rtk_rpc_exit);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
