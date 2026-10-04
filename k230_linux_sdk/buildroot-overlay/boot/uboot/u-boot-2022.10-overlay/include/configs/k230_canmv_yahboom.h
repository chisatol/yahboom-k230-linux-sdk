#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/sizes.h>

#define CONFIG_SYS_INIT_RAM_ADDR 0x80300000
#define CONFIG_SYS_INIT_RAM_SIZE 0x100000

#define CONFIG_SYS_CACHELINE_SIZE   64

#define CONFIG_SYS_NS16550_MEM32
#define DWC2_UTMI_WIDTH 16

#define DEFAULT_BOOTCMD_ENV "bootcmd=k230_boot auto auto_boot; \0"

#define CONFIG_EXTRA_ENV_SETTINGS \
	"dtb_addr=0xa000000 \0" \
	"fdt_high=0xa100000\0" \
	"kernel_addr=0xc100000\0" \
	"ramdisk_addr=0xa100000\0" \
	"console_port=console=ttyS0,115200\0" \
	DEFAULT_BOOTCMD_ENV \
	"\0"

#endif /* __CONFIG_H */
