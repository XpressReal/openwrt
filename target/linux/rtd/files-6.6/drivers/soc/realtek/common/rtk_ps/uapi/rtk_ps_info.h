#ifndef __RTK_PS_INFO_H__
#define __RTK_PS_INFO_H__
#include <linux/ioctl.h>
#define RTK_PS_INFO_IOC_MAGIC       'C'
#define COMM_LEN 128
struct rtk_ps_info_getcomm_data {
	int pid;
	char comm[COMM_LEN];
};

struct rtk_ps_info_checkcomm_data {
    char comm[COMM_LEN];
    int pid;
};

#define RTK_PS_INFO_IOC_GETCOMM      _IOWR(RTK_PS_INFO_IOC_MAGIC, 1, struct rtk_ps_info_getcomm_data)
#define RTK_PS_INFO_IOC_CHECKCOMM    _IOWR(RTK_PS_INFO_IOC_MAGIC, 2, struct rtk_ps_info_checkcomm_data)
#endif // end of __RTK_PS_INFO_H__
