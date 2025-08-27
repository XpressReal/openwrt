#ifndef __TEE_MEM_TA_H
#define __TEE_MEM_TA_H

enum ta_cmds {
	TA_TEE_MEM_PROTECTED_DESTROY_SSID   = 24,
	TA_TEE_MEM_PROTECTED_CREATE_SSID    = 26,
	TA_TEE_MEM_PROTECTED_CHANGE         = 27,
	TA_TEE_MEM_PROTECTED_EXT_SET_SSID   = 28,
	TA_TEE_MEM_PROTECTED_EXT_UNSET_SSID = 29,
};

struct tee_mem_region {
	unsigned int type;
	unsigned long long base;
	unsigned long long size;
};

struct tee_mem_ext_region {
	unsigned int ext;
	unsigned long long base;
	unsigned long long size;
	long long parent_ssid;
};

struct tee_mem_protected_create_ssid {
	struct tee_mem_region   mem;
	long long               ssid;
};

struct tee_mem_protected_destroy_ssid {
	long long               ssid;
};

struct tee_mem_protected_change_ssid {
	struct tee_mem_region   mem;
	long long               ssid;
};

struct tee_mem_protected_ext_set_ssid {
	struct tee_mem_ext_region mem;
	long long ssid;
};

struct tee_mem_protected_ext_unset_ssid {
	long long ssid;
};

#endif
