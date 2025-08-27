ccflags-y += -I$(src)

obj-$(CONFIG_RTK_HSE) += rtk-hse.o

rtk-hse-y += platform.o
rtk-hse-y += engine.o
rtk-hse-y += cq.o
rtk-hse-y += cmd.o
rtk-hse-y += device.o
CFLAGS_dma.o += -I$(srctree)/drivers/dma
rtk-hse-$(CONFIG_RTK_HSE_DMA) += dma.o
