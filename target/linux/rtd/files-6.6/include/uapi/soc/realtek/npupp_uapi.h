#ifndef __NPUPP_UAPI_H__
#define __NPUPP_UAPI_H__

/* Use 'r' as magic number */
#define NPUPP_IOC_MAGIC 'r'

#define NPUPP_INIT _IOWR(NPUPP_IOC_MAGIC, 1, unsigned int)
#define NPUPP_DESTROY _IOWR(NPUPP_IOC_MAGIC, 2, unsigned int)
#define NPUPP_TRANSCODE _IOWR(NPUPP_IOC_MAGIC, 3, unsigned int)
#define NPUPP_GET_DMA_ADDR _IOWR(NPUPP_IOC_MAGIC, 10, unsigned int)

struct npupp_addr_info {
	int handle;
	unsigned long long addr;
};

struct npupp_transcode_info {
	int src_width;
	int src_height;
	int src_stride;

	int width;
	int height;
	int stride;
	int type;
	int format;

	int srcfd;
	int dstfd;
	int srcfd_uv;
};

#endif
