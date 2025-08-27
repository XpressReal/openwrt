/*
 * Copyright (c) 2022 Realtek Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define dev_fmt(fmt) "[TEE_ACL-%d] " fmt , __LINE__
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/tee_drv.h>
#include "optee_private.h"
#include "optee_acl_util.h"

/******** Config definition **********/
//#define RTK_ACL_DEBUG

/******** Parameter definition *******/
#define RTK_ACL_DEF_SID 0xffff0
struct optee_acl_data *rtk_acl_data = NULL;

/**** Static function definition *****/
static int optee_acl_parse_dt(struct device *dev, struct optee_acl_data *data);
static void optee_acl_free_array(struct device *dev);
static void optee_acl_active_delete_all(void);
static void optee_acl_active_free(struct RTK_ACL_active *acl_active);

static struct RTK_ACL_active * optee_acl_active_search_session(uint32_t session_id);
static bool optee_acl_array_search(uint32_t acl_uuid[4], bool swap_uuid);
static bool optee_acl_get_process_info(char *process_name, uint32_t *process_name_size,
		uint32_t *user_id, uint32_t *group_id, uint32_t *process_id);

/******** Main Function definition ***/
static int dump_acl_info(uint32_t ta_uuid[4], char *process_name, uint32_t process_name_size,
        uint32_t session_id, uint32_t process_id, uint32_t user_id, uint32_t group_id)
{
    char buf[200];
    char buf_str[] = "name:";
    int pos = 0;

    if (!process_name) return 0;

    memset(buf, 0, sizeof(buf));
	pos = scnprintf(buf, 20, "[ACL-%d] ", __LINE__);
    if (ta_uuid) {
        pos += scnprintf(buf + pos, 50,
                "\033[1;31mTA:%08x-%08x\033[m | ", ta_uuid[0], ta_uuid[1]);
    }
    pos += scnprintf(buf + pos, 20, "ssid:%2d | ", session_id);
    pos += scnprintf(buf + pos, 30, "p:%4d u:%4d g:%4d | ", process_id, user_id, group_id);
    pos += scnprintf(buf + pos, 10, "sz:%2d | ", process_name_size);
    pos += scnprintf(buf + pos, sizeof(buf_str) + process_name_size,
            "%s%s", buf_str, process_name);
    pr_err("%s\n", buf);

    return 0;
}

static uint32_t swap_u32(uint32_t value)
{
    uint32_t out = 0;
    out |= (value & 0x000000FF) << 24;
    out |= (value & 0x0000FF00) << 8;
    out |= (value & 0x00FF0000) >> 8;
    out |= (value & 0xFF000000) >> 24;
    return out;
}

static bool optee_acl_swap_u32_buffer(bool swap, uint32_t *input, uint32_t *output, uint32_t count)
{
    uint32_t idx = 0;

    if (!input || count == 0 || !output) {
        return false;
    }
    for (idx = 0 ; idx < count ; idx++) {
        output[idx] = (swap) ? swap_u32(input[idx]) : (input[idx]);
    }
    return true;
}

static void optee_acl_free_array(struct device *dev)
{
    int index_ta = 0;
    int index_ca = 0;
    struct RTK_ACL *rtk_acl_array = NULL;
    struct RTK_ACL_ca *ca_info = NULL;
    uint32_t ca_info_size = 0;

    if (!rtk_acl_data)
        return;

    rtk_acl_array = rtk_acl_data->acl_array;
    if (rtk_acl_array) {
        for (index_ta = 0; index_ta < rtk_acl_data->acl_size; index_ta++) {
            ca_info = rtk_acl_array[index_ta].ca_info;
            ca_info_size = rtk_acl_array[index_ta].ca_info_size;
            if (ca_info) {
                for (index_ca = 0; index_ca < ca_info_size; index_ca++) {
                    if (ca_info[index_ca].process_name) {
                        devm_kfree(dev, ca_info[index_ca].process_name);
                        ca_info[index_ca].process_name = NULL;
                    }
                    if (ca_info[index_ca].user_id_list) {
                        devm_kfree(dev, ca_info[index_ca].user_id_list);
                        ca_info[index_ca].user_id_list = NULL;
                    }
                    if (ca_info[index_ca].group_id_list) {
                        devm_kfree(dev, ca_info[index_ca].group_id_list);
                        ca_info[index_ca].group_id_list = NULL;
                    }
                }
                devm_kfree(dev, rtk_acl_array[index_ta].ca_info);
                rtk_acl_array[index_ta].ca_info = NULL;
            }
        }
        devm_kfree(dev, rtk_acl_data->acl_array);
        rtk_acl_data->acl_array = NULL;
    }

    return;
}

bool optee_acl_is_enabled(void)
{
    if (!rtk_acl_data) {
        return false;
    }
    return rtk_acl_data->enable;
}

RTK_ACL_MODE optee_acl_mode(void)
{
    if (!rtk_acl_data) {
        return RTK_ACL_PRODUCTION;
    }
    return rtk_acl_data->mode;
}

RTK_ACL_LEVEL optee_acl_level(void)
{
    if (!rtk_acl_data) {
        return RTK_ACL_LEVEL_INVALID;
    }
    return rtk_acl_data->level;
}

/* ==============================
 * ===== Device Tree sample =====
 *   optee {
 *       compatible = "linaro,optee-tz";
 *       method = "smc";
 *       rtk,acl {
 *           rtk,acl-enable;
 *           rtk,acl-mode = <0>;
 *           rtk,acl-level = <1>;
 *           rtk,acl-array = <&rtkacl_ta_helloworld>;
 *           rtkacl_ta_helloworld: rtkacl_ta@9999 {
 *               ta-uuid = <0x8aaaf200 0x245011e4 0xabe20002 0xa5d5c51b>;
 *               ca-list = <&rtkacl_ca_helloworld>;
 *           };
 *           rtkacl_ca_helloworld: rtkacl_ca@9999 {
 *               process-name = "tee_helloworld";
 *               uid = <0>;
 *               gid = <0>;
 *           };
 *       };
 *   };
 * ==============================
 */
static int optee_acl_parse_dt(struct device *dev, struct optee_acl_data *data)
{
    int ret = -ENODATA;
    int index_ta = 0, index_ca = 0;
    struct device_node *node = NULL;
    struct device_node *acl_ta_np = NULL;
    struct device_node *acl_ca_np = NULL;

    if (!dev || !data)
        return ret;

    /* Get rtk_acl device node */
    data->dev_nd = of_get_child_by_name(dev->of_node, "rtk,acl");
    if (!data->dev_nd) {
        dev_err(dev, "%d: rtk_acl null child / disable", __LINE__);
        ret = 0;
        goto out;
    }
    node = data->dev_nd;

    data->enable = of_property_read_bool(node, "rtk,acl-enable");
    if (!data->enable) {
        dev_err(dev, "Info: rtk_acl Disable:%d", data->enable);
        ret = 0;
        goto out;
    }

    ret = of_property_read_u32(node, "rtk,acl-mode", &data->mode);
    if (ret) {
        dev_err(dev, "acl-mode not found");
        goto out;
    }

    ret = of_property_read_u32(node, "rtk,acl-level", &data->level);
    if (ret) {
        dev_err(dev, "acl-level not found");
        goto out;
    }

    // Parsing ACL TA array
    data->acl_size = of_count_phandle_with_args(node, "rtk,acl-array", NULL);
    if (data->acl_size <= 0) {
        dev_err(dev, "bad acl size:%d", data->acl_size);
        ret = -ENODATA;
        goto out;
    }

    data->acl_array = devm_kzalloc(dev, (sizeof(struct RTK_ACL) * data->acl_size), GFP_KERNEL);
    if (!data->acl_array) {
        dev_err(dev, "Fail to allocate memory for ACL TA array.");
        ret = -ENOMEM;
        goto out;
    }

    dev_info(dev, "rtk_acl mode:%s level:%d acl_size:%d ret:%#x\n",
            (data->mode == RTK_ACL_DEBUGGING) ? "debugging" : "production",
            data->level, data->acl_size, ret);

    for (index_ta = 0; index_ta < data->acl_size; index_ta++) {
        acl_ta_np = of_parse_phandle(node, "rtk,acl-array", index_ta);
        ret = of_property_read_u32_array(acl_ta_np, "ta-uuid", &data->acl_array[index_ta].ta_uuid[0], 4);
        if (ret) {
            goto err;
        }

        data->acl_array[index_ta].ca_info_size = of_count_phandle_with_args(acl_ta_np, "ca-list", NULL);
        if (data->acl_array[index_ta].ca_info_size <= 0) {
            dev_err(dev, "bad ca_info_size:%d", data->acl_array[index_ta].ca_info_size);
            ret = -ENODATA;
            goto err;
        }
#if 0
        dev_err(dev, "ta_uuid: %x-%x-%x-%x\n",
                data->acl_array[index_ta].ta_uuid[0], data->acl_array[index_ta].ta_uuid[1],
                data->acl_array[index_ta].ta_uuid[2], data->acl_array[index_ta].ta_uuid[3]);
        dev_err(dev, "acl_ca_size: %d\n", data->acl_array[index_ta].ca_info_size);
#endif

        data->acl_array[index_ta].ca_info = devm_kzalloc(dev,
                (sizeof(struct RTK_ACL_ca) * data->acl_array[index_ta].ca_info_size),
                GFP_KERNEL);
        if (!data->acl_array[index_ta].ca_info) {
            dev_err(dev, "Fail to allocate memory for ACL CA array.");
            ret = -ENOMEM;
            goto err;
        }

        // Parsing ACL CA array
        for (index_ca = 0; index_ca < data->acl_array[index_ta].ca_info_size; index_ca++) {
            struct property *prop = NULL;
            struct RTK_ACL_ca *rtk_ca_info = &data->acl_array[index_ta].ca_info[index_ca];
            int tmp = 0;
            int offset = 0;

            acl_ca_np = of_parse_phandle(acl_ta_np, "ca-list", index_ca);

            // Read Process Name
            prop = of_find_property(acl_ca_np, "process-name", &rtk_ca_info->process_name_size);
            if (rtk_ca_info->process_name_size <= 0) {
                dev_err(dev, "bad process_name_size:%d", rtk_ca_info->process_name_size);
                ret = -ENODATA;
                goto err;
            }
            rtk_ca_info->process_name = devm_kzalloc(dev,
                    rtk_ca_info->process_name_size, GFP_KERNEL);
            if (!rtk_ca_info->process_name) {
                dev_err(dev, "Fail to allocate memory for process name.");
                ret = -ENOMEM;
                goto err;
            }
            memcpy(rtk_ca_info->process_name, prop->value, rtk_ca_info->process_name_size);
            //dev_err(dev, "process_name_size: %d\n", rtk_ca_info->process_name_size);
            //dev_err(dev, "process_name: %s\n", rtk_ca_info->process_name);

            // Parsing User ID
            if (!of_get_property(acl_ca_np, "uid", &tmp)) {
                dev_err(dev, "Fail to get_property uid");
                ret = -ENODATA;
                goto err;
            }
            rtk_ca_info->user_id_size = tmp/sizeof(u32);
            if (rtk_ca_info->user_id_size <= 0) {
                dev_err(dev, "bad user_id_size:%d", rtk_ca_info->user_id_size);
                ret = -ENODATA;
                goto err;
            }
            rtk_ca_info->user_id_list = devm_kzalloc(dev,
                    (sizeof(uint32_t)*rtk_ca_info->user_id_size), GFP_KERNEL);
            if (!rtk_ca_info->user_id_list) {
                dev_err(dev, "Fail to allocate memory for UID array.");
                ret = -ENOMEM;
                goto err;
            }
            for (offset = 0; offset < rtk_ca_info->user_id_size; offset++) {
                of_property_read_u32_index(acl_ca_np, "uid", offset, &rtk_ca_info->user_id_list[offset]);
                //dev_err(dev, "uid[%d]: %d\n", offset, rtk_ca_info->user_id_list[offset]);
            }

            // Parsing Group ID
            if (!of_get_property(acl_ca_np, "gid", &tmp)) {
                dev_err(dev, "Fail to get_property gid");
                ret = -ENODATA;
                goto err;
            }
            rtk_ca_info->group_id_size = tmp/sizeof(u32);
            if (rtk_ca_info->group_id_size <= 0) {
                dev_err(dev, "bad group_id_size:%d", rtk_ca_info->group_id_size);
                ret = -ENODATA;
                goto err;
            }
            rtk_ca_info->group_id_list = devm_kzalloc(dev,
                    (sizeof(uint32_t)*rtk_ca_info->group_id_size), GFP_KERNEL);
            if (!rtk_ca_info->group_id_list) {
                dev_err(dev, "Fail to allocate memory for GID array.");
                ret = -ENOMEM;
                goto err;
            }
            for (offset = 0; offset < rtk_ca_info->group_id_size; offset++ ) {
                of_property_read_u32_index(acl_ca_np, "gid", offset, &rtk_ca_info->group_id_list[offset]);
                //dev_err(dev, "gid[%d]: %d\n", offset, rtk_ca_info->group_id_list[offset]);
            }
        }
    }
    ret = 0;

    goto out;
err:
    optee_acl_free_array(dev);
out:
    return ret;
}

static bool optee_acl_array_search(uint32_t acl_uuid[4], bool swap_uuid)
{
    struct RTK_ACL *rtk_acl_array = NULL;
    int index_ta = 0;
    int index_ca = 0;

	uint32_t ta_uuid[4] = {0};
	char process_name[NAME_MAX] = {0};
	uint32_t process_name_size = 0;
	uint32_t user_id = 0;
	uint32_t group_id = 0;
	uint32_t process_id = 0;

    if (!optee_acl_is_enabled()) {
        return true;
    }
    rtk_acl_array = rtk_acl_data->acl_array;

    if (!acl_uuid) {
		pr_err("[TEE_ACL-%d] Bad acl_uuid\n", __LINE__);
        return false;
    }

	if (!optee_acl_swap_u32_buffer(swap_uuid, &acl_uuid[0], &ta_uuid[0], 4)) {
		pr_err("[TEE_ACL-%d] Fail to swap TA UUID.\n", __LINE__);
        return false;
	}

    if (rtk_acl_array) {
        for ( index_ta = 0; index_ta < rtk_acl_data->acl_size; index_ta++ ) {
            if ( !memcmp((void *)ta_uuid, (void *)rtk_acl_array[index_ta].ta_uuid, 4 * sizeof(uint32_t)) ) {
                //pr_err("[TEE_ACL] TA UUID matched. %x-%x-%x-%x\n", ta_uuid[0], ta_uuid[1], ta_uuid[2], ta_uuid[3]);
                // TA UUID matched
                if ( rtk_acl_array[index_ta].ca_info ) {
					if (process_name_size == 0) {
						if (!optee_acl_get_process_info(&process_name[0], &process_name_size, &user_id, &group_id, &process_id)) {
							pr_err("[TEE_ACL-%d] optee_acl_get_process_info fail\n", __LINE__);
							return false;
						}
					}

                    for ( index_ca = 0; index_ca < rtk_acl_array[index_ta].ca_info_size; index_ca++ ) {
                        struct RTK_ACL_ca *tmp_acl_ca = &rtk_acl_array[index_ta].ca_info[index_ca];
                        int offset = 0;
                        bool uid_matched = false;
                        bool gid_matched = false;
                        uint32_t cmp_size = (process_name_size == 16 && (tmp_acl_ca->process_name_size - 1) < 16 ) ?
                                            (tmp_acl_ca->process_name_size - 1) : process_name_size;

                        if ( cmp_size == (tmp_acl_ca->process_name_size - 1)
                             && !memcmp(tmp_acl_ca->process_name, process_name, cmp_size) ) {
                            //pr_err("[TEE_ACL] CA process name matched: %s\n", process_name);
                            // CA process name matched
                            for ( offset = 0 ; offset < tmp_acl_ca->user_id_size ; offset++ ) {
                                if (tmp_acl_ca->user_id_list[offset] == user_id) {
                                    uid_matched = true;
                                    //pr_err("[TEE_ACL] User ID matched: %d\n", user_id);
                                    break;
                                }
                            }
                            for ( offset = 0 ; offset < tmp_acl_ca->group_id_size ; offset++ ) {
                                if (tmp_acl_ca->group_id_list[offset] == group_id) {
                                    gid_matched = true;
                                    //pr_err("[TEE_ACL] User ID matched: %d\n", group_id);
                                    break;
                                }
                            }

                            if (uid_matched && gid_matched) {
                                // user ID and Group ID matched
                                return true;
                            }
                            goto out;
                        }
                    }
                }
                goto out;
            }
        }
        if (optee_acl_level() <= RTK_ACL_LEVEL_1) {
            //pr_err("[TEE_ACL] Detect TA not in checklist, bypass checking:%d\n", optee_acl_level());
            //dump_acl_info(ta_uuid, process_name, process_name_size, RTK_ACL_DEF_SID, 0, user_id, group_id);
            return true;
        }
    }

out:
    dump_acl_info(ta_uuid, process_name, process_name_size, RTK_ACL_DEF_SID, 0, user_id, group_id);
    return false;
}

static bool optee_acl_get_process_info(char *process_name, uint32_t *process_name_size,
		uint32_t *user_id, uint32_t *group_id, uint32_t *process_id)
{
	bool ret = false;
	struct mm_struct *mm = NULL;
	char *p_name = NULL;
	uint32_t p_size = 0;
	char *pathbuf = NULL, *ptr;
	kuid_t uid;
	kgid_t gid;

	if (!process_name || !process_name_size ||
		!user_id || !group_id || !process_id) {
		pr_err("[TEE_ACL-%d] Bad parameter\n", __LINE__);
		return false;
	}

	mm = get_task_mm(current);
	if (mm && mm->exe_file) {
		pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!pathbuf) {
			ret = false;
			goto put_mm;
		}

		p_name = file_path(mm->exe_file, pathbuf, PATH_MAX);
		if (IS_ERR(p_name)) {
			ret = false;
			goto free_buf;
		}

		/* Get name_only */
		ptr = strrchr(p_name, '/');
		if (ptr)
			p_name = ptr + 1;

		p_size = strnlen(p_name, NAME_MAX);
	} else {
		p_name = current->comm;
		p_size = strnlen(current->comm, TASK_COMM_LEN);
	}

	strncpy(process_name, p_name, p_size);
	*process_name_size = p_size;

	current_uid_gid(&uid, &gid);
    *user_id =  from_kuid(&init_user_ns, uid);
    *group_id = from_kgid(&init_user_ns, gid);
    *process_id = task_pid_nr(current);

#if defined(RTK_ACL_DEBUG)
	//if (mm)
	{
        uint32_t *ta_uuid = NULL;
        dump_acl_info(ta_uuid, process_name, *process_name_size, RTK_ACL_DEF_SID,
                *process_id, *user_id, *group_id);
    }
#endif

	ret = true;
free_buf:
	if (pathbuf) kfree(pathbuf);
put_mm:
	if (mm) mmput(mm);
	return ret;
}

#define optee_acl_active_print(holder) \
{ \
    dump_acl_info((holder)->ta_uuid, (holder)->process_name, (holder)->process_name_size, \
                  (holder)->session_id, \
                  (holder)->process_id, (holder)->user_id, (holder)->group_id); \
}

static void optee_acl_active_print_all(char *log_str)
{
    struct RTK_ACL_active *holder = NULL;
    int cnt = 0;

    pr_err("[TEE_ACL] %s: optee_acl_active_print_all:\n",
            (log_str != NULL) ? log_str : "NA");

    if (!rtk_acl_data)
        return;

    mutex_lock(&rtk_acl_data->acl_lock);
    holder = rtk_acl_data->acl_active_head;
    while (holder) {
        optee_acl_active_print(holder);
        holder = holder->next;
        cnt++;
    }
    mutex_unlock(&rtk_acl_data->acl_lock);

    pr_err("[TEE_ACL] optee_acl_active_print_all done --- cnt:%d\n", cnt);
}

static void optee_acl_active_free(struct RTK_ACL_active *acl_active)
{
    if (acl_active) {
        if (acl_active->process_name) {
            kfree(acl_active->process_name);
            acl_active->process_name = NULL;
        }
        kfree(acl_active);
        acl_active = NULL;
    }
}

bool optee_acl_active_insert(uint32_t session_id, uint32_t acl_uuid[4], bool swap_uuid)
{
    struct RTK_ACL_active *holder = NULL;
	uint32_t ta_uuid[4] = {0};
	char process_name[NAME_MAX] = {0}; // 255
	uint32_t process_name_size = 0;
	uint32_t user_id = 0;
	uint32_t group_id = 0;
	uint32_t process_id = 0;

    if (!optee_acl_is_enabled()) {
        return true;
    }

    if (!acl_uuid) {
		pr_err("[TEE_ACL-%d] Bad acl_uuid\n", __LINE__);
        return false;
    }

	if (!optee_acl_swap_u32_buffer(swap_uuid, &acl_uuid[0], &ta_uuid[0], 4)) {
		pr_err("[TEE_ACL-%d] Fail to swap TA UUID.\n", __LINE__);
        return false;
	}

	if (!optee_acl_get_process_info(&process_name[0], &process_name_size, &user_id, &group_id, &process_id)) {
		pr_err("[TEE_ACL-%d] optee_acl_get_process_info fail\n", __LINE__);
        return false;
	}

    /* In the definition of tee_ta_session, "Session handle = 0" is invalid. */
    if (session_id == 0) {
        pr_err("[TEE_ACL] Detect bad session_id:%d\n", session_id);
        dump_acl_info(ta_uuid, process_name, process_name_size, session_id, process_id, user_id, group_id);
        return false;
    }

    holder = optee_acl_active_search_session(session_id);
    if (holder) {
		pr_err("[TEE_ACL] Detect ssid: %d found in the ACL active.\n", session_id);
        return false;
    } else {
        holder = kmalloc(sizeof(struct RTK_ACL_active), GFP_KERNEL);
        if (!holder) {
            pr_err("[TEE_ACL] Fail to allocate RTK_ACL_active\n");
            goto err;
        }
        memcpy((void *)&holder->ta_uuid[0], (void *)&ta_uuid[0], TEE_IOCTL_UUID_LEN);
        holder->process_name = kmalloc(process_name_size, GFP_KERNEL);
        if (!holder) {
            pr_err("[TEE_ACL] Fail to allocate process_name\n");
            goto err;
        }
        memcpy(holder->process_name, process_name, process_name_size);
        holder->process_name_size = process_name_size;
        holder->user_id = user_id;
        holder->group_id = group_id;
        holder->process_id = process_id;
        holder->session_id = session_id;

        mutex_lock(&rtk_acl_data->acl_lock);
        holder->next = rtk_acl_data->acl_active_head;
        rtk_acl_data->acl_active_head = holder;
        mutex_unlock(&rtk_acl_data->acl_lock);
    }
#if defined(RTK_ACL_DEBUG)
    optee_acl_active_print_all("active_insert");
#endif
    return true;
err:
    optee_acl_active_free(holder);

    return false;
}

static struct RTK_ACL_active * optee_acl_active_search_session(uint32_t session_id)
{
    struct RTK_ACL_active *holder = NULL;

    if (!optee_acl_is_enabled()) {
        return NULL;
    }

    mutex_lock(&rtk_acl_data->acl_lock);
    holder = rtk_acl_data->acl_active_head;
    while (holder) {
        if (holder->session_id == session_id) {
#if defined(RTK_ACL_DEBUG)
            //pr_err("[TEE_ACL] ACL active found\n");
            //optee_acl_active_print(holder);
#endif
            break;
        }
        holder = holder->next;
    }
    mutex_unlock(&rtk_acl_data->acl_lock);

    return holder;
}

/* case FUNC_OPEN:
 *  input: uuid,    unuse: session
 * case FUNC_INVOKE/FUNC_CLOSE:
 *  input: session, unuse: uuid
 *
 * Return:
 * 0 - OK, otherwise error code
 */
int optee_acl_list_check(RTK_ACL_FUNC func, u32 session, u32 uuid[4])
{
	struct RTK_ACL_active *holder = NULL;
	u32 *acl_uuid = NULL;
	bool swap_uuid = false;

	if (!optee_acl_is_enabled()) {
		return 0; /* acl is disable */
	}

	switch (func) {
	case RTK_ACL_FUNC_OPEN:
		if (!uuid)
			return -EINVAL;

		acl_uuid = uuid;
		swap_uuid = true;
		break;
	case RTK_ACL_FUNC_INVOKE: /* FALLTHROUGH */
	case RTK_ACL_FUNC_CLOSE:
		holder = optee_acl_active_search_session(session);
		if (!holder) {
			pr_err("[TEE_ACL] func[%d]: Session ID: %d not found in the ACL active.\n", func, session);
			return -EINVAL;
		}

		acl_uuid = &holder->ta_uuid[0];
		swap_uuid = false;
		break;
	default:
		pr_err("[TEE_ACL] bad func[%d] case\n", func);
		return -EINVAL;
	}

	// Restrict CA should match the information in ACL from device tree
	if (!optee_acl_array_search(acl_uuid, swap_uuid)) {
		pr_err("[TEE_ACL] func[%d]: Cannot find the process in ACL.", func);
		if (optee_acl_mode() == RTK_ACL_PRODUCTION) {
			return -EINVAL;
		}
	}

	return 0;
}

static bool optee_acl_deleteNode(struct RTK_ACL_active** head_ref, uint32_t session_id)
{
    struct RTK_ACL_active* curr = *head_ref;
    struct RTK_ACL_active* prev = NULL;

    /* Check head node */
    if (curr != NULL && curr->session_id == session_id) {
        *head_ref = curr->next;
        optee_acl_active_free(curr);
        return true;
    }

    /* Find the id of a deleted node */
    while (curr != NULL && curr->session_id != session_id) {
        prev = curr;
        curr = curr->next;
    }

    /* If not found any node, return */
    if (curr == NULL) return false;

    /* Delete node */
    prev->next = curr->next;
    optee_acl_active_free(curr);

    return true;
}

static bool optee_acl_removeAllNodes(struct RTK_ACL_active** head_ref)
{
    struct RTK_ACL_active* curr = *head_ref;
    struct RTK_ACL_active* next = NULL;

    while (curr != NULL) {
        next = curr->next;
        optee_acl_active_free(curr);
        curr = next;
    }
    *head_ref = NULL;

    return true;
}

void optee_acl_active_delete(uint32_t session_id)
{
    if (!optee_acl_is_enabled()) {
        return;
    }

    mutex_lock(&rtk_acl_data->acl_lock);
    if (!optee_acl_deleteNode(&rtk_acl_data->acl_active_head, session_id)) {
        pr_err("[TEE_ACL] Delete failed. session ID: %d\n", session_id);
    }
    mutex_unlock(&rtk_acl_data->acl_lock);

#if defined(RTK_ACL_DEBUG)
    optee_acl_active_print_all("active_delete");
#endif
}

static void optee_acl_active_delete_all(void)
{
    if (!optee_acl_is_enabled()) {
        return;
    }

    mutex_lock(&rtk_acl_data->acl_lock);
    if (!optee_acl_removeAllNodes(&rtk_acl_data->acl_active_head)) {
        pr_err("[TEE_ACL] removeAll failed.");
    }
    mutex_unlock(&rtk_acl_data->acl_lock);

#if defined(RTK_ACL_DEBUG)
    optee_acl_active_print_all("active_delete_all");
#endif
}

static ssize_t acl_ta_list_show(struct device *dev,
                      struct device_attribute *attr, char *buf)
{
    optee_acl_active_print_all("Show acl active list");

    return scnprintf(buf, PAGE_SIZE, "Show acl active list done\n");
}

static DEVICE_ATTR_RO(acl_ta_list);

static struct attribute *tee_acl_attributes[] = {
    &dev_attr_acl_ta_list.attr,
    NULL
};

static const struct attribute_group tee_acl_attr_group = {
    .attrs = tee_acl_attributes
};

int optee_acl_register(struct device *dev)
{
    int ret = -EFAULT;

    if (!dev) {
        pr_err("[TEE_ACL] %s: Detect NULL dev\n", __func__);
        return ret;
    }
    dev_info(dev, "[%s] init", __func__);

    if (rtk_acl_data) {
        dev_err(dev, "Detect acl_data is already set, error\n");
        return ret;
    }

    rtk_acl_data = devm_kzalloc(dev, sizeof(struct optee_acl_data), GFP_KERNEL);
    if (!rtk_acl_data) {
        dev_err(dev, "Fail to allocate memory for rtk_acl_data.");
        return -ENOMEM;
    }
    mutex_init(&rtk_acl_data->acl_lock);

    ret = optee_acl_parse_dt(dev, rtk_acl_data);
    if (ret) {
        dev_err(dev, "Fail to optee_acl_parse_dt\n");
        goto err;
    }

    ret = sysfs_create_group(&dev->kobj, &tee_acl_attr_group);
    if (ret)
        goto err;

    return 0;
err:
    devm_kfree(dev, rtk_acl_data);
    rtk_acl_data = NULL;
    return ret;
}

int optee_acl_unregister(struct device *dev)
{
    if (!optee_acl_is_enabled()) {
        return 0;
    }

    optee_acl_active_delete_all();
    optee_acl_free_array(dev);
    sysfs_remove_group(&dev->kobj, &tee_acl_attr_group);

    return 0;
}
