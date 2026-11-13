// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Board Initialization & Boot Mode Dispatch
 */

#include <asm/io.h>
#include <dm.h>
#include <net.h>
#include <clk-uclass.h>
#include <dt-bindings/clk/zx297520v3.h>
#include <command.h>
#include <env.h>
#include <linux/delay.h>
#include <vsprintf.h>
#include <version.h>
#include <asm/global_data.h>
#if IS_ENABLED(CONFIG_FAILSAFE)
#include <failsafe/zx_flash.h>
#include <failsafe/zx_stock_boot.h>
#endif

#define RM_MOD_CLKSEL		(0x13b000 + 0x3c)
#define RM_MOD_CLKDIV		(0x13b000 + 0x48)
#define RM_TIMER_SEL		1
#define RM_TIMER_DIV1		0

extern void zx_gic_secure_setup(void);

int board_early_init_f(void) {
	int val;

	writel_relaxed(readl_relaxed(RM_MOD_CLKSEL) | RM_TIMER_SEL, RM_MOD_CLKSEL);
	writel_relaxed((readl_relaxed(RM_MOD_CLKDIV) & (~0xf)) | RM_TIMER_DIV1, RM_MOD_CLKDIV);

	// setup arch timer:
	// setup frequency
	val = CFG_SYS_HZ_CLOCK;
	asm volatile("mcr p15, 0, %0, c14, c0, 0" : : "r" (val));

	// allow non-secure access
	asm volatile("mrc p15, 0, %0, c1, c1, 2" : "=r" (val));
	val |= BIT(10) | BIT(11);
	asm volatile("mcr p15, 0, %0, c1, c1, 2" : : "r" (val));

	asm volatile("mrc p15, 0, %0, c1, c1, 0" : "=r" (val));
	val |= BIT(10);
	asm volatile("mcr p15, 0, %0, c1, c1, 0" : : "r" (val));

	// just in case phys timer is not started
	val = 1;
	asm volatile("mcr p15, 0, %0, c14, c2, 1" : : "r" (val));

	zx_gic_secure_setup();

	return 0;
}

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	/* Address of ATAGS boot parameters for Linux kernel */
	gd->bd->bi_boot_params = 0x20440100;
	gd->bd->bi_arch_number = 7523;
#if (U_BOOT_VERSION_NUM > 2026) || (U_BOOT_VERSION_NUM == 2026 && U_BOOT_VERSION_NUM_PATCH >= 7)
	gd->dram[0].start = 0x20440000;
	gd->dram[0].size  = 29097984; /* 0x01BB8000 (~27.75MB for AP Linux) */
#else
	gd->bd->bi_dram[0].start = 0x20440000;
	gd->bd->bi_dram[0].size  = 29097984; /* 0x01BB8000 (~27.75MB for AP Linux) */
#endif
	return 0;
}

/*
 * ZX297520V3 GPIO input helper
 * GPIO0: 0..127 at 0x0013D000
 * GPIO1: 128..155 at 0x0013E000
 * RECV_REG(gpio) = BASE + ((gpio >> 4) * 16 + 5) * 4
 */
static int zx_gpio_get_input(unsigned int gpio_id)
{
	uint32_t addr;
	if (gpio_id < 128)
		addr = 0x0013D000 + ((gpio_id >> 4) * 16 + 5) * 4;
	else
		addr = 0x0013E000 + (((gpio_id - 128) >> 4) * 16 + 5) * 4;
	return (readl(addr) >> (gpio_id % 16)) & 0x1;
}

/*
 * Check if the Reset button is being held down.
 * Active low: 0 = pressed, 1 = released.
 * Checks for hold_seconds. Returns 1 if held continuously.
 */
static int __maybe_unused check_reset_button_held(unsigned int gpio_id, int hold_seconds)
{
	int loops = hold_seconds * 10;
	int pressed_count = 0;

	for (int i = 0; i < loops; i++) {
		if (zx_gpio_get_input(gpio_id) == 0) {
			pressed_count++;
		} else {
			return 0; // Released early
		}
		mdelay(100);
	}
	return (pressed_count >= loops * 8 / 10);
}

#define ZX29_DEFAULT_RESET_GPIO 52 /* PIN_KPD_RST from ZTE uSDK */

#define SYS_SOC_CRM_BASE (0x0013b000)
#define SYS_SOC_MOD_CLKSEL (SYS_SOC_CRM_BASE + 0x54)
#define SYS_SOC_MOD_RSTEN  (SYS_SOC_CRM_BASE + 0x8c)
#define SYS_SOC_MOD_USBSTATECTRL (SYS_SOC_CRM_BASE + 0x90)

void zx29_usb_phy_init(void)
{
	/* 1. Reset USB 2.0 PHY and controller */
	writel(readl(SYS_SOC_MOD_RSTEN) | (0x3 << 5), SYS_SOC_MOD_RSTEN);
	udelay(20);

	/* 2. Select USB bus clock (156MHz) */
	writel(readl(SYS_SOC_MOD_CLKSEL) & ~0x3, SYS_SOC_MOD_CLKSEL);
	udelay(20);

	/* 3. Release USB PHY reset */
	writel(readl(SYS_SOC_MOD_RSTEN) | (1 << 4), SYS_SOC_MOD_RSTEN);
	udelay(100);
}

struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	zx29_usb_phy_init();
}

#if IS_ENABLED(CONFIG_USB_ETHER)
extern int usb_ether_init(void);

int zx29_usb_ether_register(void)
{
	static int registered = 0;
	if (registered)
		return 0;

	zx29_usb_phy_init();
	int ret = usb_ether_init();
	if (ret == 0) {
		registered = 1;
		printf("[USB RNDIS] USB Ethernet Gadget (RNDIS) initialized by default.\n");
		if (!env_get("ethact"))
			env_set("ethact", "usb_ether");
		return 0;
	} else {
		printf("[USB RNDIS] usb_ether_init returned %d\n", ret);
		return ret;
	}
}
#else
int zx29_usb_ether_register(void) { return 0; }
#endif

int board_late_init(void)
{
	struct udevice *clk_dev;
	int ret;
	const char *bootmode __maybe_unused;

	ret = uclass_get_device_by_driver(UCLASS_CLK, DM_DRIVER_GET(zx297520v3_matrix_clk), &clk_dev);
	if (ret) {
		printf("bad uclass\n");
	} else {
		struct clk clk = { .id = ZX297520_CPU_WCLK, .dev = clk_dev };
		printf("CPU running at %lu MHz\n", clk_get_rate(&clk) / 1000000);
	}

#if IS_ENABLED(CONFIG_USB_ETHER)
	/* Initialize USB RNDIS Ethernet Gadget by default at boot time */
	zx29_usb_ether_register();
	env_set("ethact", "usb_ether");
	env_set("ethprime", "usb_ether");
	env_set("ethrotate", "no");
#endif
	if (!env_get("ethaddr"))
		env_set("ethaddr", "02:29:75:20:00:01");

#if IS_ENABLED(CONFIG_FAILSAFE)
	printf("\n======================================================\n");
	printf("  ZX297520V3 Web Failsafe & Multi-Partition Recovery  \n");
	printf("======================================================\n");
	zx_flash_init();

	/* Auto-configure bootargs with detected partition table if not explicitly set */
	if (!env_get("bootargs")) {
		char mtdparts_buf[512];
		char auto_bargs[768];
		if (zx_flash_generate_mtdparts(mtdparts_buf, sizeof(mtdparts_buf)) == 0) {
			snprintf(auto_bargs, sizeof(auto_bargs),
				 ZX_STD_BOOTARGS_BASE " %s", mtdparts_buf);
			env_set("bootargs", auto_bargs);
			printf("[BOOT] Auto-detected partition table -> generated bootargs:\n       %s\n", auto_bargs);
		}
	}
#endif

	/*
	 * Boot path selection via shared IRAM0 (CFG_BOOT_MODE_SAVE_ADDR_FOR_UBOOT):
	 * - 0x82002bc4 is written by the stage-1 loader on Cortex-M0 before
	 *   releasing Cortex-A53:
	 *     0x12345678 (ZLOAD_MODE): booted from Flash by stock 0.zloader
	 *     0x87654321 (TLOAD_MODE): booted via USB by stock 0.tloader
	 *     0x00000000             : booted via USB by openloader (IRAM::init
	 *                              clears 0x82000400..0x82003404)
	 *
	 * When USB boot is detected (load_mode != 0x12345678), enter Web Failsafe
	 * mode (httpd) directly without attempting to boot stock flash firmware.
	 */
	u32 load_mode = readl((void __iomem *)0x82002bc4);
	int usb_loaded = (load_mode != 0x12345678);

	printf("\n[BOOT] Load mode @0x82002bc4 = 0x%08x -> %s\n",
	       load_mode,
	       usb_loaded ? "USB Boot (Direct Web Failsafe)" : "Flash Boot (ZLOAD)");

	if (usb_loaded) {
#if IS_ENABLED(CONFIG_FAILSAFE)
		printf("[BOOT] USB boot detected -> entering Web Failsafe mode directly...\n");
		printf("[sys_entry]: Access web interface at http://192.168.72.1/\n");
		env_set("bootcmd", "httpd");
		run_command("httpd", 0);
#endif
		return 0;
	}

	/* Standard flash boot path */
	bootmode = env_get("bootmode");
	if (bootmode && !strcmp(bootmode, "downloader")) {
		printf("[sys_entry]: Starting ZTE Downloader Mode...\n");
		run_command("downloader", 0);
	} else if (bootmode && !strcmp(bootmode, "shell")) {
		printf("[sys_entry]: Entering U-Boot interactive shell...\n");
	} else if (bootmode && !strcmp(bootmode, "failsafe")) {
		printf("[sys_entry]: bootmode=failsafe -> Starting Web Failsafe (httpd)...\n");
		printf("[sys_entry]: Access web interface at http://192.168.72.1/\n");
		run_command("httpd", 0);
	} else {
		int enter_failsafe = 0;
		int has_reset_gpio = 1;
		unsigned int reset_gpio = ZX29_DEFAULT_RESET_GPIO;
		const char *rst_str = env_get("reset_gpio");
		const char *always_failsafe = env_get("failsafe_always");

		if (rst_str && (!strcmp(rst_str, "none") || !strcmp(rst_str, "-1"))) {
			has_reset_gpio = 0;
		} else if (rst_str && strlen(rst_str) > 0) {
			reset_gpio = simple_strtoul(rst_str, NULL, 10);
		}

		if (always_failsafe && !strcmp(always_failsafe, "1")) {
			enter_failsafe = 1;
		} else {
			if (has_reset_gpio) {
				printf("\n[BOOT] Checking Reset key (GPIO %u) and serial input...\n", reset_gpio);
				printf("[BOOT] Hold RESET button for 3s, or press 'f'/'w' to enter Web Failsafe\n");
			} else {
				printf("\n[BOOT] Press 'f'/'w' for Web Failsafe, Ctrl+C for U-Boot shell...\n");
			}
			printf("[BOOT] (Timeout: 3s, default: autoboot -> auto-fallback to failsafe on error)\n");

			for (int s = 3; s > 0 && !enter_failsafe; s--) {
				printf("\r[BOOT] Autoboot in %d second%s (M0 HB: 0x%08x)... ", s, s > 1 ? "s" : "",
				       readl((void __iomem *)0x82002cf8));
				for (int ms = 0; ms < 10; ms++) {
					// Check Reset button pressed (active low) if reset_gpio is configured
					if (has_reset_gpio && zx_gpio_get_input(reset_gpio) == 0) {
						printf("\n[BOOT] Reset button pressed (GPIO %u)! Checking 3s hold...\n", reset_gpio);
						if (check_reset_button_held(reset_gpio, 2)) {
							printf("[BOOT] Reset button held for 3s -> Entering Web Failsafe!\n");
							enter_failsafe = 1;
							break;
						}
					}
					// Check UART console key
					if (tstc()) {
						int c = getchar();
						if (c == 'f' || c == 'F' || c == 'w' || c == 'W') {
							printf("\n[BOOT] Web Failsafe requested via console key '%c'!\n", c);
							enter_failsafe = 1;
							break;
						} else if (c == 0x03 || c == ' ' || c == 'c' || c == 'C') {
							printf("\n[BOOT] Aborted to U-Boot interactive shell.\n");
							return 0;
						}
					}
					mdelay(100);
				}
			}
			printf("\n");
		}

		if (enter_failsafe) {
#if IS_ENABLED(CONFIG_FAILSAFE)
			printf("[sys_entry]: Starting Web Failsafe Recovery Mode (httpd)...\n");
			printf("[sys_entry]: Access web interface at http://192.168.72.1/\n");
			printf("[sys_entry]: Press Ctrl+C in console to abort to U-Boot shell\n");
			run_command("httpd", 0);
#endif
		} else {
			const char *bootcmd = env_get("bootcmd");
			if (bootcmd && strlen(bootcmd) > 0 && strcmp(bootcmd, "fastboot usb 0") != 0 && strcmp(bootcmd, "stock_boot") != 0) {
				printf("[BOOT] Running bootcmd: %s\n", bootcmd);
				ret = run_command(bootcmd, 0);
				if (ret != 0) {
					printf("[BOOT] bootcmd returned error (%d)!\n", ret);
				}
			} else {
#if IS_ENABLED(CONFIG_FAILSAFE)
				/* Attempt booting ZTE stock Linux from imagefs */
				ret = zx_boot_stock_linux();
				if (ret != 0) {
					printf("[BOOT] Stock Linux boot failed or imagefs not found!\n");
				}
#endif
			}
			// If bootcmd or stock boot not set or failed, auto-fallback to Web Failsafe
#if IS_ENABLED(CONFIG_FAILSAFE)
			printf("[BOOT] Falling back to Web Failsafe (httpd)...\n");
			printf("[sys_entry]: Access web interface at http://192.168.72.1/\n");
			run_command("httpd", 0);
#endif
		}
	}
	/* ===== 标准路径结束 ===== */

	return 0;
}
