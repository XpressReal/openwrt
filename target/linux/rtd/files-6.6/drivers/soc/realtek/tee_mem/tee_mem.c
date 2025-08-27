// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <soc/realtek/rtk_tee.h>
#include "tee_mem_ta.h"

static const uuid_t pta_mem_uuid = UUID_INIT(0xb8b220fc, 0x3851, 0x4d5a,
	0xa6, 0x7e, 0x9d, 0x45, 0xed, 0xdf, 0xc5, 0x32);

struct tee_mem_device {
	struct tee_context *tee_context;
	struct device *dev;
	unsigned int tee_session;
	struct mutex protected_lock;
	struct list_head protected_list;
	struct list_head protected_ext_list;
};

static int tee_mem_invoke_protected_create(struct tee_mem_device *memdev,
	phys_addr_t base, size_t size, unsigned int type, long long *ssid)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct tee_mem_protected_create_ssid *info;
	int ret;

	tee_shm = tee_shm_alloc_kernel_buf(memdev->tee_context, sizeof(struct tee_mem_protected_create_ssid));
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	info = tee_shm_get_va(tee_shm, 0);
	info->mem.base = base;
	info->mem.size = size;
	info->mem.type = type;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = TA_TEE_MEM_PROTECTED_CREATE_SSID;
	arg.session = memdev->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(memdev->dev, "%s: failed to invoke func: %pe / %#x\n", __func__,
			ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}

	*ssid = info->ssid;
	tee_shm_free(tee_shm);

	return 0;
}

static int tee_mem_invoke_protected_destroy(struct tee_mem_device *memdev, long long ssid)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct tee_mem_protected_destroy_ssid *info;
	int ret;

	tee_shm = tee_shm_alloc_kernel_buf(memdev->tee_context, sizeof(struct tee_mem_protected_destroy_ssid));
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	info = tee_shm_get_va(tee_shm, 0);
	info->ssid = ssid;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = TA_TEE_MEM_PROTECTED_DESTROY_SSID;
	arg.session = memdev->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(memdev->dev, "%s: failed to invoke func: %pe / %#x\n", __func__,
			ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}
	tee_shm_free(tee_shm);

	return 0;
}

static int tee_mem_invoke_protected_change(struct tee_mem_device *memdev, long long ssid,
	phys_addr_t base, size_t size, unsigned int type)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct tee_mem_protected_change_ssid *info;
	int ret;

	tee_shm = tee_shm_alloc_kernel_buf(memdev->tee_context, sizeof(struct tee_mem_protected_change_ssid));
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	info = tee_shm_get_va(tee_shm, 0);
	info->ssid = ssid;
	info->mem.base = base;
	info->mem.size = size;
	info->mem.type = type;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = TA_TEE_MEM_PROTECTED_CHANGE;
	arg.session = memdev->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(memdev->dev, "%s: failed to invoke func: %pe / %#x\n", __func__,
			ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}

	tee_shm_free(tee_shm);

	return 0;
}

static int tee_mem_invoke_protected_ext_slot(struct tee_mem_device *memdev, phys_addr_t base,
	size_t size, unsigned int ext, unsigned int parent_ssid, long long *ssid)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct tee_mem_protected_ext_set_ssid *info;
	int ret;

	tee_shm = tee_shm_alloc_kernel_buf(memdev->tee_context, sizeof(struct tee_mem_protected_ext_set_ssid));
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	info = tee_shm_get_va(tee_shm, 0);
	info->mem.base           = base;
	info->mem.size           = size;
	info->mem.ext            = ext;
	info->mem.parent_ssid    = parent_ssid;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = TA_TEE_MEM_PROTECTED_EXT_SET_SSID;
	arg.session = memdev->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(memdev->dev, "%s: failed to invoke func: %pe / %#x\n", __func__,
			ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}

	*ssid = info->ssid;

	tee_shm_free(tee_shm);

	return 0;
}

static int tee_mem_priotected_ext_destroy(struct tee_mem_device *memdev, long long ssid)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct tee_mem_protected_ext_unset_ssid *info;
	int ret;

	tee_shm = tee_shm_alloc_kernel_buf(memdev->tee_context, sizeof(struct tee_mem_protected_ext_unset_ssid));
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	info = tee_shm_get_va(tee_shm, 0);
	info->ssid = ssid;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = TA_TEE_MEM_PROTECTED_EXT_UNSET_SSID;
	arg.session = memdev->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(memdev->dev, "%s: failed to invoke func: %pe / %#x\n", __func__,
			ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}

	tee_shm_free(tee_shm);
	return 0;
}

struct tee_mem_protected_slot {
	struct list_head        list;
	struct tee_mem_region   mem;
	long long               ssid;
};

struct tee_mem_protected_ext_slot {
	struct list_head list;
	struct tee_mem_ext_region mem;
	long long ssid;
};

static void tee_mem_protected_slot_init(struct tee_mem_protected_slot *slot,
	phys_addr_t base, size_t size, long long ssid, unsigned int type)
{
	memset((void *) slot, 0, sizeof(*slot));
	INIT_LIST_HEAD(&slot->list);
	slot->mem.base  = base;
	slot->mem.size  = size;
	slot->mem.type  = type;
	slot->ssid      = ssid;
}

struct tee_mem_protected_slot *tee_mem_protected_create(struct tee_mem_device *memdev,
        phys_addr_t base, size_t size, unsigned int type)
{
	struct tee_mem_protected_slot *slot;
	long long ssid;
	int ret;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return NULL;

	ret = tee_mem_invoke_protected_create(memdev, base, size, type, &ssid);
	if (ret < 0) {
		kfree(slot);
		return NULL;
	}

	dev_dbg(memdev->dev, "%s: ssid=%llx\n", __func__, ssid);

	tee_mem_protected_slot_init(slot, base, size, ssid, type);
	mutex_lock(&memdev->protected_lock);
	list_add(&slot->list, &memdev->protected_list);
	mutex_unlock(&memdev->protected_lock);

	return slot;
}
EXPORT_SYMBOL_GPL(tee_mem_protected_create);

int tee_mem_protected_destroy(struct tee_mem_device *memdev, struct tee_mem_protected_slot *slot)
{
	int ret;

	ret = tee_mem_invoke_protected_destroy(memdev, slot->ssid);
	if (ret)
		return ret;

	dev_dbg(memdev->dev, "%s: ssid=%llx\n", __func__, slot->ssid);

	mutex_lock(&memdev->protected_lock);
	list_del(&slot->list);
	mutex_unlock(&memdev->protected_lock);

	kfree(slot);
	return 0;
}
EXPORT_SYMBOL_GPL(tee_mem_protected_destroy);

int tee_mem_protected_change(struct tee_mem_device *memdev, struct tee_mem_protected_slot *slot,
	phys_addr_t base, size_t size, unsigned int type)
{
	int ret;

	ret = tee_mem_invoke_protected_change(memdev, slot->ssid, base, size, type);
	if (ret)
		return ret;

	dev_dbg(memdev->dev, "%s: ssid=%llx\n", __func__, slot->ssid);

	slot->mem.base = base;
	slot->mem.size = size;

	return ret;
}
EXPORT_SYMBOL_GPL(tee_mem_protected_change);

static void tee_mem_protected_ext_slot_init(struct tee_mem_protected_ext_slot *slot,
		phys_addr_t base, size_t size, long long ssid, unsigned int ext,
		struct tee_mem_protected_slot *parent)
{
	memset((void *)slot, 0, sizeof(*slot));
	INIT_LIST_HEAD(&slot->list);
	slot->mem.base          = base;
	slot->mem.size          = size;
	slot->mem.ext           = ext;
	slot->mem.parent_ssid   = parent->ssid;
	slot->ssid              = ssid;
}

struct tee_mem_protected_ext_slot *tee_mem_protected_ext_create(struct tee_mem_device *memdev,
	phys_addr_t base, size_t size, unsigned int ext, struct tee_mem_protected_slot *parent)
{
	struct tee_mem_protected_ext_slot *slot;
	long long ssid;
	int ret;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return NULL;

	ret = tee_mem_invoke_protected_ext_slot(memdev, base, size, ext, parent->ssid, &ssid);
	if (ret) {
		kfree(slot);
		return NULL;
	}

	dev_dbg(memdev->dev, "%s: ssid=%llx\n", __func__, ssid);

	tee_mem_protected_ext_slot_init(slot, base, size, ssid, ext, parent);
	mutex_lock(&memdev->protected_lock);
	list_add(&slot->list, &memdev->protected_ext_list);
	mutex_unlock(&memdev->protected_lock);

	return slot;
}
EXPORT_SYMBOL_GPL(tee_mem_protected_ext_create);

int tee_mem_protected_ext_destroy(struct tee_mem_device *memdev, struct tee_mem_protected_ext_slot *slot)
{
	int ret;

	ret = tee_mem_priotected_ext_destroy(memdev, slot->ssid);
	if (ret)
		return ret;

	dev_dbg(memdev->dev, "%s: ssid=%llx\n", __func__, slot->ssid);

	mutex_lock(&memdev->protected_lock);
	list_del(&slot->list);
	mutex_unlock(&memdev->protected_lock);

	kfree(slot);

	return 0;
}
EXPORT_SYMBOL_GPL(tee_mem_protected_ext_destroy);

static void tee_mee_protected_destroy_all(struct tee_mem_device *memdev)
{
	struct tee_mem_protected_slot *slot, *_slot;
	struct tee_mem_protected_ext_slot *slot_ext, *_slot_ext;

	list_for_each_entry_safe(slot, _slot, &memdev->protected_list, list)
		tee_mem_protected_destroy(memdev, slot);
	list_for_each_entry_safe(slot_ext, _slot_ext, &memdev->protected_ext_list, list)
		tee_mem_protected_ext_destroy(memdev, slot_ext);
}

static int tee_mem_match(struct tee_ioctl_version_data *data, const void *vers)
{
	return 1;
}

static int tee_mem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tee_mem_device *memdev;
	struct tee_ioctl_open_session_arg arg = {0};
	struct tee_param param[4];
	struct tee_ioctl_version_data vers = {
		.impl_id = TEE_OPTEE_CAP_TZ,
		.impl_caps = TEE_IMPL_ID_OPTEE,
		.gen_caps = TEE_GEN_CAP_GP,
	};
	int ret;

	memdev = devm_kzalloc(dev, sizeof(*memdev), GFP_KERNEL);
	if (!memdev)
		return -ENOMEM;

	memdev->tee_context = tee_client_open_context(NULL, tee_mem_match, NULL, &vers);
	if (IS_ERR(memdev->tee_context)) {
		ret = PTR_ERR(memdev->tee_context);
		if (ret == -ENOENT)
			ret = -EPROBE_DEFER;
		return dev_err_probe(dev, ret, "failed to open context\n");
	}

	memcpy(arg.uuid, pta_mem_uuid.b, TEE_IOCTL_UUID_LEN);
	arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	arg.num_params = 4;
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	ret = tee_client_open_session(memdev->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(dev, "failed to tee_client_open_session: %pe / %#x\n", ERR_PTR(ret), arg.ret);
		return ret ?: -EOPNOTSUPP;
	}

	memdev->tee_session = arg.session;
	memdev->dev = dev;

	INIT_LIST_HEAD(&memdev->protected_list);
	INIT_LIST_HEAD(&memdev->protected_ext_list);
	mutex_init(&memdev->protected_lock);

	platform_set_drvdata(pdev, memdev);

	return 0;
}

static int tee_mem_remove(struct platform_device *pdev)
{
	struct tee_mem_device *memdev = platform_get_drvdata(pdev);

	tee_mee_protected_destroy_all(memdev);

	tee_client_close_session(memdev->tee_context, memdev->tee_session);
	tee_client_close_context(memdev->tee_context);

	return 0;
}

static const struct of_device_id of_tee_mem_match_table[] = {
	{ .compatible = "realtek,tee-mem", },
	{}
};

static struct platform_driver tee_mem_driver = {
	.probe  = tee_mem_probe,
	.remove = tee_mem_remove,
	.driver = {
		.name           = "rtk-tee-mem",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(of_tee_mem_match_table),

	},
};

static int __init tee_mem_init(void)
{
	return platform_driver_register(&tee_mem_driver);
}
subsys_initcall_sync(tee_mem_init);

static void __exit tee_mem_exit(void)
{
	platform_driver_unregister(&tee_mem_driver);
}
module_exit(tee_mem_exit);

struct tee_mem_device *tee_mem_dev_get_simple(void)
{
	struct device *dev = platform_find_device_by_driver(NULL, &tee_mem_driver.driver);

	return dev ? dev_get_drvdata(dev) : NULL;
}
EXPORT_SYMBOL_GPL(tee_mem_dev_get_simple);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
