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
#ifndef _OPTEE_ACL_UTIL_H
#define _OPTEE_ACL_UTIL_H
/************************************************************************
 *  Include files
 ************************************************************************/
#include <linux/mutex.h>

/************************************************************************
 * Typedefs and Structure
 ************************************************************************/
/* Realtek Access Control Mode */
typedef enum {
    RTK_ACL_DEBUGGING = 0,
    RTK_ACL_PRODUCTION = 1
} RTK_ACL_MODE;

/* Realtek Access Control Function Case */
typedef enum {
    RTK_ACL_FUNC_INVOKE = 0,
    RTK_ACL_FUNC_OPEN,
    RTK_ACL_FUNC_CLOSE,
} RTK_ACL_FUNC;

/* Realtek Access Control Level */
typedef enum {
    RTK_ACL_LEVEL_1 = 1,
    RTK_ACL_LEVEL_2 = 2,
	RTK_ACL_LEVEL_INVALID
} RTK_ACL_LEVEL;

/* Realtek Access Control List from device tree */
struct RTK_ACL {
    uint32_t ta_uuid[4];
    struct RTK_ACL_ca *ca_info;
    uint32_t ca_info_size;
};

struct RTK_ACL_ca {
    char *process_name;
    uint32_t process_name_size;
    uint32_t *user_id_list;
    uint32_t user_id_size;
    uint32_t *group_id_list;
    uint32_t group_id_size;
};

/* Record the actived valid process information */
struct RTK_ACL_active {
    uint32_t ta_uuid[4];
    char *process_name;
    uint32_t process_name_size;
    uint32_t user_id;
    uint32_t group_id;
    uint32_t process_id;
    uint32_t session_id;
    uint32_t process_group_id;
    struct RTK_ACL_active *next;
};

/* Structure of Access Control Data information */
struct optee_acl_data {
	struct device_node *dev_nd;
	bool enable;
	RTK_ACL_MODE mode;
	RTK_ACL_LEVEL level;
	struct RTK_ACL *acl_array;
	int acl_size;
	struct mutex acl_lock;
	struct RTK_ACL_active *acl_active_head;
};

/************************************************************************
 *  Public function prototypes
 ************************************************************************/
/* For optee/call.c */
bool optee_acl_is_enabled(void);
RTK_ACL_MODE optee_acl_mode(void);

int optee_acl_list_check(RTK_ACL_FUNC func, u32 session, u32 uuid[4]); /* return 0 PASS, else FAIL */
bool optee_acl_active_insert(uint32_t session_id, uint32_t acl_uuid[4], bool swap_uuid);
void optee_acl_active_delete(uint32_t session_id);

/* For optee/core.c */
int optee_acl_register(struct device *dev);
int optee_acl_unregister(struct device *dev);

#endif /* _OPTEE_ACL_UTIL_H */
