// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Realtek DHC Kernel RPC Agent driver
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <soc/realtek/rtk-krpc-agent.h>

struct rtk_krpc_agent {
	struct device *dev;
	struct rpmsg_device *rpdev;
	char *name;
};


struct rtk_krpc_ept {
	struct device *dev;
	struct rpmsg_device *rpdev;
	struct rtk_krpc_agent *agent;
	struct rtk_krpc_ept_info *krpc_ept_info;
	struct rpmsg_endpoint *ept;
	struct work_struct work;
	spinlock_t queue_lock;
	struct sk_buff_head queue;
	int big_endian;
	krpc_cb cb;
	u32 id;
};

void rtk_krpc_dump_ringbuf_info(struct rtk_krpc_ept_info *krpc_ept_info)
{
	rtk_dump_all_ringbuf_info(krpc_ept_info->krpc_ept->dev->parent);
}
EXPORT_SYMBOL_GPL(rtk_krpc_dump_ringbuf_info);

int rtk_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len)
{
	if (krpc_ept_info->krpc_ept->big_endian)
		endian_swap_32_write((void *)buf, len);

	return rpmsg_send(krpc_ept_info->krpc_ept->ept, (void *)buf, len);
}
EXPORT_SYMBOL_GPL(rtk_send_rpc);

static void  krpc_work(struct work_struct *work)
{
	struct rtk_krpc_ept *krpc_ept = container_of(work, struct rtk_krpc_ept, work);
	struct sk_buff *skb;
	struct rpc_struct *rpc;

	spin_lock(&krpc_ept->queue_lock);
	if (skb_queue_empty(&krpc_ept->queue))
		return;
	skb = skb_dequeue(&krpc_ept->queue);
	spin_unlock(&krpc_ept->queue_lock);

	rpc = (struct rpc_struct *)skb->data;


	dev_dbg(krpc_ept->dev, "[%s]rpc->programID:%d, rpc->versionID:%d, rpc->procedureID:%d, rpc->taskID:%d, rpc->sysTID:%d, rpc->sysPID:%d, rpc->parameterSize:%d, rpc->mycontext:0x%x\n",
		__func__, rpc->programID, rpc->versionID, rpc->procedureID, rpc->taskID, rpc->sysTID, rpc->sysPID, rpc->parameterSize, rpc->mycontext);

	krpc_ept->cb(krpc_ept->krpc_ept_info, skb->data);

	kfree_skb(skb);
	if (!skb_queue_empty(&krpc_ept->queue))
		schedule_work(&krpc_ept->work);

}


static int rtk_krpc_callback(struct rpmsg_device *rpdev,
				void *data,
				int count,
				void *priv,
				u32 addr)
{
	struct rtk_krpc_ept *krpc_ept = priv;
	char *buf = (char *)data;
	struct sk_buff *skb;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (krpc_ept->big_endian)
		endian_swap_32_read(buf, sizeof(struct rpc_struct) + ntohl(rpc->parameterSize));

	skb = alloc_skb(count, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, count);

	spin_lock(&krpc_ept->queue_lock);
	skb_queue_tail(&krpc_ept->queue, skb);
	spin_unlock(&krpc_ept->queue_lock);

	schedule_work(&krpc_ept->work);

	return 0;
}

int krpc_info_init(struct rtk_krpc_ept_info *krpc_ept_info, char *name, krpc_cb cb)
{
	struct rtk_krpc_agent *agent = krpc_ept_info->agent;
	struct rtk_krpc_ept *krpc_ept;
	struct rpmsg_device *rpdev = agent->rpdev;
	struct rpmsg_channel_info chinfo;


	krpc_ept = kzalloc(sizeof(*krpc_ept), GFP_KERNEL);
	if (!krpc_ept)
		return -ENOMEM;

	strscpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = RPMSG_ADDR_ANY;

	krpc_ept->ept = rpmsg_create_ept(rpdev, rtk_krpc_callback, krpc_ept, chinfo);
	krpc_ept->id = krpc_ept->ept->addr;

	krpc_ept->dev = agent->dev;
	krpc_ept->cb = cb;
	krpc_ept->krpc_ept_info = krpc_ept_info;
	spin_lock_init(&krpc_ept->queue_lock);
	skb_queue_head_init(&krpc_ept->queue);
	INIT_WORK(&krpc_ept->work, krpc_work);
	krpc_ept->big_endian = rcpu_endian_check(rpdev->dev.parent);

	krpc_ept_info->krpc_ept = krpc_ept;
	krpc_ept_info->id = krpc_ept->id;
	mutex_init(&krpc_ept_info->send_mutex);
	init_completion(&krpc_ept_info->ack);
	strscpy(krpc_ept_info->name, name, sizeof(krpc_ept_info->name));

	return 0;

}
EXPORT_SYMBOL_GPL(krpc_info_init);

void krpc_info_deinit(struct rtk_krpc_ept_info *krpc_ept_info)
{
	cancel_work_sync(&krpc_ept_info->krpc_ept->work);
	rpmsg_destroy_ept(krpc_ept_info->krpc_ept->ept);
	kfree(krpc_ept_info->krpc_ept);
	krpc_ept_info->id = 0;
	krpc_ept_info->krpc_ept = NULL;
}
EXPORT_SYMBOL_GPL(krpc_info_deinit);

void krpc_ept_info_put(struct rtk_krpc_ept_info *krpc_ept_info)
{
	kfree(krpc_ept_info);
}
EXPORT_SYMBOL_GPL(krpc_ept_info_put);

struct rtk_krpc_ept_info *of_krpc_ept_info_get(struct device_node *np, int index)
{
	struct rtk_krpc_agent *agent;
	struct device_node *agent_np;
	struct platform_device *pdev;
	struct rtk_krpc_ept_info *krpc_ept_info;


	agent_np = of_parse_phandle(np, "realtek,krpc-agent", index);
	if (!agent_np) {
		pr_err("%s: failed to get krpc-agent phandle\n", np->full_name);
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(agent_np->parent->parent)) {
		pr_err("%s: agent_np is disabled\n", np->full_name);
		return ERR_PTR(-EINVAL);
	}
	pdev = of_find_device_by_node(agent_np);
	of_node_put(agent_np);
	if (!pdev) {
		pr_err("%s: cannot find agent_np device\n", np->full_name);
		return ERR_PTR(-EPROBE_DEFER);
	}

	agent = dev_get_drvdata(&pdev->dev);
	if (!agent)
		return ERR_PTR(-EPROBE_DEFER);

	krpc_ept_info = kzalloc(sizeof(*krpc_ept_info), GFP_KERNEL);
	if (!krpc_ept_info)
		return ERR_PTR(-ENOMEM);

	krpc_ept_info->agent = agent;
	krpc_ept_info->rpdev = agent->rpdev;

	return krpc_ept_info;
}
EXPORT_SYMBOL_GPL(of_krpc_ept_info_get);

static int rtk_krpc_agent_probe(struct platform_device *pdev)
{
	struct rtk_krpc_agent *agent;

	agent = devm_kzalloc(&pdev->dev, sizeof(*agent), GFP_KERNEL);
	if (!agent)
		return -ENOMEM;

	agent->rpdev =  container_of(pdev->dev.parent, struct rpmsg_device, dev);
	if (agent->rpdev == NULL) {
		dev_err(&pdev->dev, "cannot find rpmsg device\n");
		return -EPROBE_DEFER;
	}

	agent->dev = &pdev->dev;
	agent->name = (char *)of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, agent);

	dev_err(&pdev->dev, "probe\n");

	return 0;
}


static const struct of_device_id rtk_krpc_agent_of_match[] = {
	{ .compatible = "realtek,krpc-agent-acpu", .data = "acpu-agent" },
	{ .compatible = "realtek,krpc-agent-vcpu", .data = "vcpu-agent" },
	{ .compatible = "realtek,krpc-agent-ve3", .data = "ve3-agent" },
	{ .compatible = "realtek,krpc-agent-hifi", .data = "hifi-agent" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_krpc_agent_of_match);

static struct platform_driver rtk_krpc_agent_driver = {
	.probe = rtk_krpc_agent_probe,
	.driver = {
		.name = "rtk-krpc-agent",
		.of_match_table = rtk_krpc_agent_of_match,
	},
};

static int __init rtk_krpc_agent_init(void)
{
	return platform_driver_register(&rtk_krpc_agent_driver);
}
module_init(rtk_krpc_agent_init);

static void __exit rtk_krpc_agent_exit(void)
{
	platform_driver_unregister(&rtk_krpc_agent_driver);
}
module_exit(rtk_krpc_agent_exit);

MODULE_AUTHOR("TYChang <tychang@realtek.com>");
MODULE_DESCRIPTION("Realtek Kernel RPC Agent Driver");
MODULE_LICENSE("GPL v2");

