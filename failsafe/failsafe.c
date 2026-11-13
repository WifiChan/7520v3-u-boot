// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Web Failsafe & Firmware Upgrade Manager
 */

#include <command.h>
#include <dm.h>
#include <errno.h>
#include <env.h>
#include <malloc.h>
#include <net.h>
#include <time.h>
#include <net/mtk_tcp.h>
#include <net/mtk_httpd.h>
#include <u-boot/md5.h>
#include <linux/stringify.h>
#include <vsprintf.h>
#include <version.h>
#include <version_string.h>
#include <linux/delay.h>
#include <asm/global_data.h>
#include <failsafe/fw_type.h>
#include <linux/usb/gadget.h>

#include "fs.h"
#include "zx_flash.h"
#include "zx_stock_boot.h"
#include "dhcpd.h"

DECLARE_GLOBAL_DATA_PTR;

static u32 upload_data_id;
static const void *upload_data;
static size_t upload_size;
static int upgrade_success;
static int reboot_requested;
static failsafe_fw_t fw_type;
static char custom_part_buf[32];

/*
 * Designated DRAM window for uploaded firmware.
 *
 * Starts at 0x20800000 (above the 0x20000000..0x2043FFFF companion-core
 * reservation) and dynamically expands up to 512KB below U-Boot's own
 * malloc heap / stack (giving ~19.25MB on 32MB DDR and ~51MB on 64MB DDR).
 */
#define ZX_UPLOAD_BUFFER_ADDR   0x20800000
#define ZX_UPLOAD_BUFFER_SIZE   (10 * 1024 * 1024)

void *httpd_get_upload_buffer_ptr(size_t size)
{
	return (void *)ZX_UPLOAD_BUFFER_ADDR;
}

size_t httpd_get_upload_buffer_size(void)
{
	ulong limit = 0;

	if (gd) {
		if (gd->malloc_base > ZX_UPLOAD_BUFFER_ADDR)
			limit = gd->malloc_base;
		else if (gd->start_addr_sp > ZX_UPLOAD_BUFFER_ADDR)
			limit = gd->start_addr_sp;
		else if (gd->relocaddr > ZX_UPLOAD_BUFFER_ADDR)
			limit = gd->relocaddr;
	}

	/* Leave 512KB guard below U-Boot stack/heap */
	if (limit > ZX_UPLOAD_BUFFER_ADDR + (512 * 1024)) {
		size_t dyn_size = (size_t)(limit - ZX_UPLOAD_BUFFER_ADDR - (512 * 1024));

		dyn_size &= ~(size_t)0xFFFF;
		if (dyn_size > ZX_UPLOAD_BUFFER_SIZE)
			return dyn_size;
	}

	return ZX_UPLOAD_BUFFER_SIZE;
}

/*
 * Long NOR erase/write operations run synchronously from the network receive
 * path, so nothing pumps the USB device controller while they are busy and the
 * host drops the RNDIS link mid-operation.  Pump only the UDC: that services
 * endpoint 0 (RNDIS control/keepalive, link status) and the data endpoints,
 * but never mtk_tcp/httpd -- so it cannot re-enter the request handler we are
 * currently executing inside.
 */
static struct udevice *failsafe_udc;
static int failsafe_udc_resolved;
static int failsafe_httpd_running;

void zx_flash_poll(void)
{
	if (!failsafe_httpd_running)
		return;

	if (!failsafe_udc_resolved) {
		struct udevice *eth;

		failsafe_udc_resolved = 1;
		eth = eth_get_dev_by_name("usb_ether");
		if (eth)
			failsafe_udc = eth->parent;
	}

	if (failsafe_udc)
		dm_usb_gadget_handle_interrupts(failsafe_udc);

	mtk_tcp_keepalive_poll();
}

static int output_plain_file(struct httpd_response *response, const char *filename)
{
	const struct fs_desc *file;
	int ret = 0;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
		if (file->size >= 2 &&
		    ((const u8 *)file->data)[0] == 0x1f &&
		    ((const u8 *)file->data)[1] == 0x8b) {
			response->info.extra_header = "Content-Encoding: gzip\r\n";
		}
	} else {
		response->data = "Error: 404 file not found";
		response->size = strlen(response->data);
		ret = 1;
	}

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/html; charset=utf-8";

	return ret;
}

static void version_handler(enum httpd_uri_handler_status status,
			    struct httpd_request *request,
			    struct httpd_response *response)
{
	static char ver_buf[256];

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

	snprintf(ver_buf, sizeof(ver_buf),
		 "U-Boot %d.%02d [ZX297520V3 Web Failsafe]\n"
		 "Target: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)\n"
		 "Version: %s\n",
		 U_BOOT_VERSION_NUM, U_BOOT_VERSION_NUM_PATCH, version_string);

	response->data = ver_buf;
	response->size = strlen(response->data);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";
}

static void flash_info_handler(enum httpd_uri_handler_status status,
			       struct httpd_request *request,
			       struct httpd_response *response)
{
	static char info_buf[256];

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	zx_flash_get_info_str(info_buf, sizeof(info_buf));

	response->data = info_buf;
	response->size = strlen(response->data);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";
}

static void set_flash_profile_handler(enum httpd_uri_handler_status status,
				     struct httpd_request *request,
				     struct httpd_response *response)
{
	const char *q = request->query;
	u32 size = 16;

	if (status != HTTP_CB_NEW)
		return;

	if (q) {
		const char *p = strstr(q, "size=");
		if (p)
			size = simple_strtoul(p + 5, NULL, 10);
	}

	zx_flash_set_profile(size);

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";
	response->data = "OK";
	response->size = 2;
}

/*
 * Diagnostic endpoint: /memtest
 *
 * In the field this board is driven over USB with no serial console attached,
 * so there is no way to read U-Boot's boot log after something goes wrong.
 * This endpoint exposes, through the web UI instead, the numbers needed to
 * tell whether the upload window is safe to write an 8MB POST body into:
 *
 *   - how much DRAM U-Boot actually detected at runtime (ram_size / ram_top),
 *     which can differ from what the device tree declares, and
 *   - where U-Boot placed its own image, its malloc() heap and its stack.
 *
 * If the upload window overlaps any of those, the POST body overwrites live
 * U-Boot data on its way in -- which shows up as the connection dropping
 * part-way through an upload, with no serial output to explain why.
 *
 * Adding ?write=1 additionally writes a pattern through the window and reads
 * it back.  That is destructive whenever the window does overlap live memory,
 * so it is deliberately not done by default.
 */
static void memtest_handler(enum httpd_uri_handler_status status,
			    struct httpd_request *request,
			    struct httpd_response *response)
{
	static const u32 probe_off[] = {
		0x000000, 0x100000, 0x200000, 0x300000, 0x3B6000,
		0x400000, 0x600000, 0x800000, 0x9F0000
	};
	static char mt_buf[1200];
	const char *q = request->query;
	u32 i;
	int n = 0;

	if (status != HTTP_CB_NEW)
		return;

	n += snprintf(mt_buf + n, sizeof(mt_buf) - n,
		      "ram_base    = 0x%08lX\n"
		      "ram_size    = 0x%08lX (%lu MB)\n"
		      "ram_top     = 0x%08lX\n"
		      "relocaddr   = 0x%08lX\n"
		      "malloc_base = 0x%08lX\n"
		      "stack_top   = 0x%08lX\n"
		      "upload_win  = 0x%08lX .. 0x%08lX (%lu KB)\n",
		      (ulong)gd->ram_base, (ulong)gd->ram_size,
		      (ulong)(gd->ram_size / (1024 * 1024)),
		      (ulong)gd->ram_top, (ulong)gd->relocaddr,
		      (ulong)gd->malloc_base, (ulong)gd->start_addr_sp,
		      (ulong)ZX_UPLOAD_BUFFER_ADDR,
		      (ulong)(ZX_UPLOAD_BUFFER_ADDR + httpd_get_upload_buffer_size()),
		      (ulong)(httpd_get_upload_buffer_size() / 1024));

	if (q && strstr(q, "write=1")) {
		n += snprintf(mt_buf + n, sizeof(mt_buf) - n,
			      "\n-- write/read-back probe --\n");

		for (i = 0; i < sizeof(probe_off) / sizeof(probe_off[0]); i++) {
			u32 addr = ZX_UPLOAD_BUFFER_ADDR + probe_off[i];
			u32 pat = 0xA5A5C3C3 ^ probe_off[i];
			volatile u32 *p = (volatile u32 *)addr;
			u32 was = *p;
			u32 got;

			*p = pat;
			got = *p;

			n += snprintf(mt_buf + n, sizeof(mt_buf) - n,
				      "+0x%06X @0x%08X  was 0x%08X  now 0x%08X  %s\n",
				      probe_off[i], addr, was, got,
				      got == pat ? "OK" : "MISMATCH");
			if (n >= (int)sizeof(mt_buf) - 128)
				break;
		}
	} else {
		n += snprintf(mt_buf + n, sizeof(mt_buf) - n,
			      "\n(add ?write=1 to probe the window)\n");
	}

	printf("[MEMTEST]\n%s", mt_buf);

	response->status = HTTP_RESP_STD;
	response->data = mt_buf;
	response->size = n;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";
}

#define BACKUP_CHUNK_SIZE       (1024 * 1024)   /* 1MB streaming buffer for maximum throughput */

/*
 * The download (backup) stream shares the same DRAM window as the firmware
 * upload instead of keeping a second private reservation, so the board only
 * has to account for one multi-megabyte buffer.
 *
 * BACKUP_CHUNK_BUF_ADDR is therefore an alias of ZX_UPLOAD_BUFFER_ADDR, not a
 * standalone address (it used to be 0x20600000).
 *
 * Consequence: an upload and a download must never be in flight at the same
 * time -- a backup chunk read would overwrite a POST body being received, and
 * a POST body would overwrite a chunk being streamed out.  backup_handler()
 * enforces this: it refuses to start while an upload is active, and aborts an
 * in-flight stream if an upload begins underneath it.
 */
#define BACKUP_CHUNK_BUF_ADDR   ZX_UPLOAD_BUFFER_ADDR

struct backup_stream_state {
	u32 cur_offset;
	u32 end_offset;
	u32 total_size;
	int active;
	char part_name[32];
	char hdr_buf[256];
	u8 *chunk_buf;
};
static struct backup_stream_state g_bk;

static void backup_handler(enum httpd_uri_handler_status status,
			   struct httpd_request *request,
			   struct httpd_response *response)
{
	if (status == HTTP_CB_CLOSED) {
		g_bk.active = 0;
		return;
	}

	if (status == HTTP_CB_NEW) {
		char part_name[32] = "full";
		const char *q = request->query;
		u32 offset = 0, size = 0;
		int ret;

		/*
		 * Upload and backup share one DRAM window, so refuse to start a
		 * download while a POST body is still being received into it.
		 */
		if (httpd_is_uploading()) {
			printf("[BACKUP STREAM]: REFUSED, upload in progress (shared buffer busy)\n");
			response->status = HTTP_RESP_STD;
			response->info.code = 503;
			response->info.connection_close = 1;
			response->info.content_type = "text/plain; charset=utf-8";
			response->data = "Error: Busy - firmware upload in progress, retry when it finishes";
			response->size = strlen(response->data);
			return;
		}

		g_bk.chunk_buf = (u8 *)BACKUP_CHUNK_BUF_ADDR;

		if (q) {
			const char *p = strstr(q, "part=");
			if (p) {
				char *amp;
				p += 5;
				strncpy(part_name, p, sizeof(part_name) - 1);
				part_name[sizeof(part_name) - 1] = '\0';
				amp = strchr(part_name, '&');
				if (amp)
					*amp = '\0';
			}
		}

		if (!strcmp(part_name, "full") || !strcmp(part_name, "raw")) {
			offset = 0;
			size = zx_flash_get_size();
			if (size == 0) {
				response->status = HTTP_RESP_STD;
				response->info.code = 400;
				response->info.connection_close = 1;
				response->info.content_type = "text/plain; charset=utf-8";
				response->data = "Error: Flash not detected or size is 0";
				response->size = strlen(response->data);
				return;
			}
		} else {
			ret = zx_flash_find_partition(part_name, &offset, &size);
			if (ret != 0 || size == 0) {
				response->status = HTTP_RESP_STD;
				response->info.code = 404;
				response->info.connection_close = 1;
				response->info.content_type = "text/plain; charset=utf-8";
				response->data = "Error: Partition not found or flash not detected";
				response->size = strlen(response->data);
				return;
			}
		}

		printf("[BACKUP STREAM]: Starting 1MB stream '%s' @ 0x%08X (size: 0x%08X, %u KB)...\n",
		       part_name, offset, size, size / 1024);

		g_bk.cur_offset = offset;
		g_bk.end_offset = offset + size;
		g_bk.total_size = size;
		g_bk.active = 1;
		strncpy(g_bk.part_name, part_name, sizeof(g_bk.part_name) - 1);

		int hlen = snprintf(g_bk.hdr_buf, sizeof(g_bk.hdr_buf),
				    "HTTP/1.1 200 OK\r\n"
				    "Content-Type: application/octet-stream\r\n"
				    "Content-Length: %u\r\n"
				    "Content-Disposition: attachment; filename=\"zx297520v3_%s_backup.bin\"\r\n"
				    "Connection: close\r\n\r\n",
				    size, part_name);

		response->status = HTTP_RESP_CUSTOM;
		response->data = g_bk.hdr_buf;
		response->size = hlen;
		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		/*
		 * An upload that started mid-download would be using the same DRAM
		 * window as the chunk below.  Stop the stream rather than ship a
		 * silently corrupted backup.
		 */
		if (g_bk.active && httpd_is_uploading()) {
			printf("[BACKUP STREAM]: ABORTED at 0x%08X, upload took over the shared buffer\n",
			       g_bk.cur_offset);
			response->status = HTTP_RESP_NONE;
			g_bk.active = 0;
			return;
		}

		if (g_bk.active && g_bk.cur_offset < g_bk.end_offset) {
			u32 rem = g_bk.end_offset - g_bk.cur_offset;
			u32 chunk = rem > BACKUP_CHUNK_SIZE ? BACKUP_CHUNK_SIZE : rem;

			if (zx_flash_read(g_bk.cur_offset, g_bk.chunk_buf, chunk) < 0) {
				printf("[BACKUP STREAM]: Read error at 0x%08X\n", g_bk.cur_offset);
				response->status = HTTP_RESP_NONE;
				g_bk.active = 0;
				return;
			}

			g_bk.cur_offset += chunk;
			response->status = HTTP_RESP_CUSTOM;
			response->data = (char *)g_bk.chunk_buf;
			response->size = chunk;
		} else {
			response->status = HTTP_RESP_NONE;
			g_bk.active = 0;
		}
	}
}

static void flash_chunk_handler(enum httpd_uri_handler_status status,
				struct httpd_request *request,
				struct httpd_response *response)
{
	static char resp_json[128];

	if (status != HTTP_CB_NEW)
		return;

	struct httpd_form_value *val_chunk = httpd_request_find_value(request, "chunk");
	struct httpd_form_value *val_part = httpd_request_find_value(request, "part");
	struct httpd_form_value *val_off = httpd_request_find_value(request, "offset");
	struct httpd_form_value *val_len = httpd_request_find_value(request, "len");
	struct httpd_form_value *val_mode = httpd_request_find_value(request, "mode");
	int mode = 0;		/* 0 legacy erase+write, 1 erase-only, 2 write-only */

	if (val_mode) {
		if (!strncmp(val_mode->data, "erase", 5))
			mode = 1;
		else if (!strncmp(val_mode->data, "write", 5))
			mode = 2;
	}

	/*
	 * The erase pass carries no data body (it just burns the range), so it is
	 * identified by mode=erase plus an explicit length; every other mode still
	 * requires the chunk blob to program.
	 */
	if (!val_part || !val_off || (mode == 1 ? !val_len : !val_chunk)) {
		response->status = HTTP_RESP_STD;
		response->info.code = 400;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->data = "{\"status\":\"error\",\"msg\":\"Missing parameters\"}";
		response->size = strlen(response->data);
		return;
	}

	char part_name[32] = {0};
	u32 len = val_part->size < sizeof(part_name) ? val_part->size : sizeof(part_name) - 1;
	memcpy(part_name, val_part->data, len);
	part_name[len] = '\0';

	u32 chunk_offset = simple_strtoul(val_off->data, NULL, 10);
	u32 part_base = 0, part_max_size = 0;

	if (!strcmp(part_name, "raw") || !strcmp(part_name, "full")) {
		part_base = 0;
		part_max_size = zx_flash_get_size();
	} else {
		if (zx_flash_find_partition(part_name, &part_base, &part_max_size) != 0) {
			response->status = HTTP_RESP_STD;
			response->info.code = 404;
			response->info.connection_close = 1;
			response->info.content_type = "application/json";
			response->data = "{\"status\":\"error\",\"msg\":\"Partition not found\"}";
			response->size = strlen(response->data);
			return;
		}
	}

	u32 chunk_len = (mode == 1) ? simple_strtoul(val_len->data, NULL, 10)
				    : val_chunk->size;
	u32 flash_addr;
	u32 bl_off = 0, bl_size = 0;
	int has_bootloader;

	/*
	 * Safety: reject anything that would run past the end of the target
	 * region.  part_max_size was computed above but never checked, so a chunk
	 * whose offset/length ran past the partition -- or past the end of the
	 * flash entirely -- used to be erased and written anyway.
	 */
	if (!part_max_size || chunk_offset >= part_max_size ||
	    chunk_len > part_max_size - chunk_offset) {
		printf("[FLASH_CHUNK]: REJECT out of range: '%s' off=0x%X len=0x%X region=0x%X\n",
		       part_name, chunk_offset, chunk_len, part_max_size);
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"error\",\"msg\":\"Chunk out of range: offset 0x%X + length 0x%X exceeds region 0x%X\"}",
			 chunk_offset, chunk_len, part_max_size);
		response->status = HTTP_RESP_STD;
		response->info.code = 400;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->data = resp_json;
		response->size = strlen(resp_json);
		return;
	}

	/*
	 * Safety: the web UI never offers the first-stage bootloader, so a request
	 * naming it can only come from a hand-crafted form.  Refuse it outright,
	 * and warn (without blocking) about a full/raw dump that legitimately
	 * spans the bootloader area.
	 */
	has_bootloader = (zx_flash_find_partition("zloader", &bl_off, &bl_size) == 0);

	if (!strcmp(part_name, "zloader")) {
		printf("[FLASH_CHUNK]: REJECT write to protected partition 'zloader'\n");
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"error\",\"msg\":\"Partition 'zloader' is write-protected\"}");
		response->status = HTTP_RESP_STD;
		response->info.code = 403;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->data = resp_json;
		response->size = strlen(resp_json);
		return;
	}

	if (has_bootloader && bl_size &&
	    chunk_offset < bl_off + bl_size && chunk_offset + chunk_len > bl_off)
		printf("[FLASH_CHUNK]: WARNING '%s' chunk [0x%X,0x%X) overlaps bootloader [0x%X,0x%X)\n",
		       part_name, chunk_offset, chunk_offset + chunk_len,
		       bl_off, bl_off + bl_size);

	flash_addr = zx_flash_logical_to_physical(part_base, chunk_offset);

	/*
	 * Idempotent retry protection: if the host retried the exact same
	 * /flash_chunk request because the previous HTTP response was lost,
	 * return the cached result immediately without re-erasing/re-writing.
	 */
	static int last_chunk_valid;
	static int last_chunk_mode;
	static u32 last_chunk_addr;
	static u32 last_chunk_len;
	static ulong last_chunk_erase_ms;
	static ulong last_chunk_write_ms;

	if (last_chunk_valid && last_chunk_mode == mode &&
	    last_chunk_addr == flash_addr && last_chunk_len == chunk_len) {
		printf("[FLASH_CHUNK]: Retry detected for phys=0x%08X len=0x%X mode=%d, returning cached OK\n",
		       flash_addr, chunk_len, mode);
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"ok\",\"mode\":%d,\"written\":%u,\"offset\":%u,"
			 "\"erase_ms\":%lu,\"write_ms\":%lu}",
			 mode, chunk_len, chunk_offset,
			 last_chunk_erase_ms, last_chunk_write_ms);
		response->status = HTTP_RESP_STD;
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->data = resp_json;
		response->size = strlen(resp_json);
		return;
	}

	printf("[FLASH_CHUNK]: '%s' log_off=0x%X -> phys=0x%08X, len 0x%X (%u KB) mode=%d...\n",
	       part_name, chunk_offset, flash_addr, chunk_len, chunk_len / 1024, mode);

	/*
	 * Two-pass flashing: mode=erase burns the range without touching the data
	 * body, mode=write programs an already-erased range (so no second erase),
	 * and mode=0 keeps the legacy combined erase+write for older callers.
	 */
	ulong erase_ms = 0;
	ulong write_ms = 0;

	if (mode != 2) {
		ulong t_erase = get_timer(0);

		if (zx_flash_erase(flash_addr, chunk_len) != 0) {
			printf("[FLASH_CHUNK]: Erase failed @ 0x%08X: %s\n",
			       flash_addr, zx_flash_get_last_error());
			snprintf(resp_json, sizeof(resp_json),
				 "{\"status\":\"error\",\"msg\":\"Flash erase failed @ 0x%08X: %s\"}",
				 flash_addr, zx_flash_get_last_error());
			response->status = HTTP_RESP_STD;
			response->info.code = 500;
			response->info.connection_close = 1;
			response->info.content_type = "application/json";
			response->data = resp_json;
			response->size = strlen(resp_json);
			return;
		}
		erase_ms = get_timer(t_erase);
	}

	if (mode != 1) {
		ulong t_write = get_timer(0);

		if (zx_flash_write(flash_addr, val_chunk->data, chunk_len) != 0) {
			printf("[FLASH_CHUNK]: Write failed @ 0x%08X: %s\n",
			       flash_addr, zx_flash_get_last_error());
			snprintf(resp_json, sizeof(resp_json),
				 "{\"status\":\"error\",\"msg\":\"Flash write failed @ 0x%08X: %s\"}",
				 flash_addr, zx_flash_get_last_error());
			response->status = HTTP_RESP_STD;
			response->info.code = 500;
			response->info.connection_close = 1;
			response->info.content_type = "application/json";
			response->data = resp_json;
			response->size = strlen(resp_json);
			return;
		}
		write_ms = get_timer(t_write);
	}

	last_chunk_valid = 1;
	last_chunk_mode = mode;
	last_chunk_addr = flash_addr;
	last_chunk_len = chunk_len;
	last_chunk_erase_ms = erase_ms;
	last_chunk_write_ms = write_ms;

	snprintf(resp_json, sizeof(resp_json),
		 "{\"status\":\"ok\",\"mode\":%d,\"written\":%u,\"offset\":%u,"
		 "\"erase_ms\":%lu,\"write_ms\":%lu}",
		 mode, chunk_len, chunk_offset, erase_ms, write_ms);

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->data = resp_json;
	response->size = strlen(resp_json);
}

static void get_partitions_handler(enum httpd_uri_handler_status status,
				   struct httpd_request *request,
				   struct httpd_response *response)
{
	static char json_buf[2048];
	char *p = json_buf;
	u32 count, i;
	char name[32];
	u32 offset, size;

	if (status != HTTP_CB_NEW)
		return;

	count = zx_flash_get_part_count();
	p += snprintf(p, sizeof(json_buf) - (p - json_buf),
		      "{\"has_ptable\":%d,\"parts\":[",
		      zx_flash_has_partition_table());

	for (i = 0; i < count; i++) {
		if (zx_flash_get_partition_info(i, name, &offset, &size) == 0) {
			p += snprintf(p, sizeof(json_buf) - (p - json_buf),
				      "%s{\"name\":\"%s\",\"offset\":\"0x%08X\",\"size\":\"%u KB\"}",
				      (i > 0) ? "," : "", name, offset, size / 1024);
		}
	}
	p += snprintf(p, sizeof(json_buf) - (p - json_buf), "]}");

	response->status = HTTP_RESP_STD;
	response->data = json_buf;
	response->size = strlen(response->data);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json; charset=utf-8";
}

static void get_bootargs_handler(enum httpd_uri_handler_status status,
				 struct httpd_request *request,
				 struct httpd_response *response)
{
	static char bargs[768];
	const char *env_args;

	if (status != HTTP_CB_NEW)
		return;

	env_args = env_get("bootargs");
	if (!env_args || !env_args[0]) {
		char mtdparts_buf[512];
		if (zx_flash_generate_mtdparts(mtdparts_buf, sizeof(mtdparts_buf)) == 0) {
			snprintf(bargs, sizeof(bargs),
				 ZX_STD_BOOTARGS_BASE " %s", mtdparts_buf);
		} else {
			snprintf(bargs, sizeof(bargs), "%s", ZX_STD_BOOTARGS_BASE);
		}
	} else {
		strncpy(bargs, env_args, sizeof(bargs) - 1);
	}
	bargs[sizeof(bargs) - 1] = '\0';

	response->status = HTTP_RESP_STD;
	response->data = bargs;
	response->size = strlen(response->data);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";
}

static void set_bootargs_handler(enum httpd_uri_handler_status status,
				 struct httpd_request *request,
				 struct httpd_response *response)
{
	static char dec_buf[512];
	const char *q = request->query;
	const char *p;
	char *d;

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain; charset=utf-8";

	if (!q) {
		response->data = "FAIL: Missing query string";
		response->size = strlen(response->data);
		return;
	}

	p = strstr(q, "args=");
	if (!p) {
		response->data = "FAIL: Missing args parameter";
		response->size = strlen(response->data);
		return;
	}
	p += 5;

	d = dec_buf;
	while (*p && d < dec_buf + sizeof(dec_buf) - 1) {
		if (*p == '+') {
			*d++ = ' ';
			p++;
		} else if (*p == '%' && p[1] && p[2]) {
			char hex[3] = { p[1], p[2], '\0' };
			*d++ = (char)simple_strtoul(hex, NULL, 16);
			p += 3;
		} else {
			*d++ = *p++;
		}
	}
	*d = '\0';

	printf("[BOOTARGS]: Updated bootargs to: '%s'\n", dec_buf);
	env_set("bootargs", dec_buf);
	env_save();

	response->data = "OK: bootargs saved";
	response->size = strlen(response->data);
}

static void index_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "index.html");
}

static void upload_handler(enum httpd_uri_handler_status status,
			   struct httpd_request *request,
			   struct httpd_response *response)
{
	static char md5_str[33] = "";
	static char resp[128];
	struct httpd_form_value *fw = NULL;
	struct httpd_form_value *custom = NULL;
	u8 md5_sum[16];
	int i;
	static const char hexchars[] = "0123456789abcdef";

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";

	/* 1. full (Full Flash Programmer Dump) */
	fw = httpd_request_find_value(request, "full");
	if (fw) {
		fw_type = FW_TYPE_FULL;
		goto done;
	}

	/* 2. rootfs (Root Filesystem) */
	fw = httpd_request_find_value(request, "rootfs");
	if (fw) {
		fw_type = FW_TYPE_ROOTFS;
		goto done;
	}

	/* 3. imagefs (Kernel / System Image) */
	fw = httpd_request_find_value(request, "imagefs");
	if (!fw)
		fw = httpd_request_find_value(request, "firmware");
	if (fw) {
		fw_type = FW_TYPE_IMAGEFS;
		goto done;
	}

	/* 4. uboot */
	fw = httpd_request_find_value(request, "uboot");
	if (fw) {
		fw_type = FW_TYPE_UBOOT;
		goto done;
	}

	/* 5. nvrofs (NV Calibration Data) */
	fw = httpd_request_find_value(request, "nvrofs");
	if (fw) {
		fw_type = FW_TYPE_NVROFS;
		goto done;
	}

	/* 6. userdata (User Data Partition) */
	fw = httpd_request_find_value(request, "userdata");
	if (fw) {
		fw_type = FW_TYPE_USERDATA;
		goto done;
	}

	/* 7. zloader (Stage 1 Bootloader) */
	fw = httpd_request_find_value(request, "zloader");
	if (fw) {
		fw_type = FW_TYPE_ZLOADER;
		goto done;
	}

	/* 7. Custom partition / offset */
	fw = httpd_request_find_value(request, "custom");
	if (fw) {
		fw_type = FW_TYPE_CUSTOM;
		custom = httpd_request_find_value(request, "custom_target");
		if (custom && custom->size > 0 && custom->size < sizeof(custom_part_buf)) {
			strncpy(custom_part_buf, (const char *)custom->data, custom->size);
			custom_part_buf[custom->size] = '\0';
		} else {
			strcpy(custom_part_buf, "0x00420000");
		}
		goto done;
	}

	response->data = "fail";
	response->size = strlen(response->data);
	return;

done:
	upload_data_id = upload_id;
	upload_data = fw->data;
	upload_size = fw->size;

	/* Calculate MD5 checksum in 256KB slices while polling USB/TCP keep-alive */
	{
		MD5Context md5_ctx;
		const unsigned char *cur = (const unsigned char *)fw->data;
		size_t rem = fw->size;

		MD5Init(&md5_ctx);
		while (rem > 0) {
			size_t step = rem > (256 * 1024) ? (256 * 1024) : rem;

			MD5Update(&md5_ctx, cur, step);
			cur += step;
			rem -= step;
			zx_flash_poll();
		}
		MD5Final(md5_sum, &md5_ctx);
	}
	for (i = 0; i < 16; i++) {
		u8 hex = (md5_sum[i] >> 4) & 0xf;
		md5_str[i * 2] = hexchars[hex];
		hex = md5_sum[i] & 0xf;
		md5_str[i * 2 + 1] = hexchars[hex];
	}
	md5_str[32] = '\0';

	snprintf(resp, sizeof(resp), "%ld %s", (long)fw->size, md5_str);
	response->data = resp;
	response->size = strlen(response->data);
}

/*
 * Step sizes for /flash_step:
 * - FLASH_ERASE_STEP_SIZE (1MB): Pass 1 (total erase) slices large flash
 *   erases into 1MB keep-alive steps while still erasing the entire target
 *   region before Pass 2 begins.
 * - FLASH_STEP_SIZE (256KB): Pass 2 (write) programming granularity.
 */
#define FLASH_STEP_SIZE       (256 * 1024)
#define FLASH_ERASE_STEP_SIZE (1024 * 1024)

/*
 * Slice-by-slice programmer for the "upload, then confirm" path.
 *
 * The browser POSTs the whole image once to /upload (it lands in the shared DRAM
 * window), then repeatedly GETs /flash_step.  Each call processes one slice and
 * answers with a JSON progress record, so flashing.html can render a live
 * progress bar instead of an opaque spinner.  Because every request only does a
 * short burst of work, the USB/RNDIS link also stays fed (the flash loops pump
 * the gadget through zx_flash_poll()) and the response is a normal
 * Content-Length-delimited reply, so it always reaches the browser.
 *
 * The caller selects the stage with ?mode=erase or ?mode=write.  The browser
 * runs the erase pass first, then the write pass.  The erase pass burns the
 * whole region in a single request; the write pass then walks it in
 * FLASH_STEP_SIZE slices.  Omitting mode keeps the legacy combined
 * erase+write pass, which is also sliced.
 */
static void flash_step_handler(enum httpd_uri_handler_status status,
			       struct httpd_request *request,
			       struct httpd_response *response)
{
	static char resp_json[192];
	static u32 step_cursor;
	static u32 step_total;
	static int step_active;
	static int step_phase;		/* 0 legacy, 1 erase, 2 write */
	static u32 step_upload_id;
	static u32 last_step_offset;
	static u32 last_step_written;
	static u32 last_step_erase_ms;
	static u32 last_step_write_ms;
	static ulong last_step_elapsed_ms;
	static int last_step_valid;
	const char *q = request->query;
	const char *off_p = NULL;
	ulong t_start, ms;
	u32 slice, done;
	u32 erase_ms = 0, write_ms = 0;
	int phase = 0;
	int op;
	int ret;

	if (status == HTTP_CB_RESPONDING || status == HTTP_CB_CLOSED) {
		if (upgrade_success)
			mtk_tcp_reset_all_conn();
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (q) {
		if (strstr(q, "mode=erase"))
			phase = 1;
		else if (strstr(q, "mode=write"))
			phase = 2;
		off_p = strstr(q, "offset=");
	}

	if (upload_data_id != upload_id || !upload_data || !upload_size) {
		step_active = 0;
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"error\",\"msg\":\"没有待写入的固件，请重新上传\"}");
		goto send;
	}

	/* A phase change (or new upload) restarts the per-phase cursor */
	if (!step_active || step_phase != phase || step_upload_id != upload_id) {
		step_total = upload_size;
		step_cursor = 0;
		step_phase = phase;
		step_upload_id = upload_id;
		step_active = 1;
		last_step_valid = 0;
	}

	/*
	 * Idempotent retry protection: if the browser retried the exact same
	 * slice (matching offset == last_step_offset) because the previous HTTP
	 * response was lost over USB RNDIS, return the cached progress record
	 * immediately instead of advancing or re-flashing the next slice.
	 */
	if (off_p) {
		u32 req_off = simple_strtoul(off_p + 7, NULL, 10);

		if (last_step_valid && req_off == last_step_offset &&
		    step_cursor == last_step_written) {
			done = (step_cursor >= step_total);
			snprintf(resp_json, sizeof(resp_json),
				 "{\"status\":\"ok\",\"phase\":%d,\"written\":%u,\"total\":%u,\"done\":%u,"
				 "\"elapsed_ms\":%lu,\"erase_ms\":%u,\"write_ms\":%u}",
				 phase, step_cursor, step_total, done,
				 last_step_elapsed_ms, last_step_erase_ms,
				 last_step_write_ms);
			goto send;
		}
		if (req_off == 0 && step_cursor >= step_total) {
			step_cursor = 0;
			last_step_valid = 0;
		}
	}

	if (step_cursor >= step_total) {
		done = 1;
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"ok\",\"phase\":%d,\"written\":%u,\"total\":%u,\"done\":1,"
			 "\"elapsed_ms\":%lu,\"erase_ms\":%u,\"write_ms\":%u}",
			 phase, step_total, step_total,
			 last_step_elapsed_ms, last_step_erase_ms,
			 last_step_write_ms);
		goto send;
	}

	/*
	 * Pass 1 (phase == 1) performs a complete total erase across the target
	 * region in 1MB keep-alive steps (whilst zx_flash_poll() also sends
	 * USB RNDIS & TCP ACK keep-alives). Pass 2 (phase == 2) programs the
	 * image in 256KB slices.
	 */
	u32 cur_offset = step_cursor;
	u32 max_slice = (phase == 1) ? FLASH_ERASE_STEP_SIZE : FLASH_STEP_SIZE;
	slice = step_total - step_cursor;
	if (slice > max_slice)
		slice = max_slice;

	op = (phase == 1) ? ZX_FLASH_OP_ERASE :
	     (phase == 2) ? ZX_FLASH_OP_WRITE : ZX_FLASH_OP_BOTH;

	t_start = get_timer(0);
	ret = zx_flash_write_slice(upload_data, step_total, fw_type,
				   custom_part_buf, step_cursor, slice, op,
				   &erase_ms, &write_ms);
	ms = get_timer(t_start);

	if (ret) {
		step_active = 0;
		snprintf(resp_json, sizeof(resp_json),
			 "{\"status\":\"error\",\"msg\":\"%s失败: %s\"}",
			 (phase == 1) ? "擦除" : "写入",
			 zx_flash_get_last_error());
		response->info.code = 500;
		goto send;
	}

	step_cursor += slice;
	done = (step_cursor >= step_total);

	last_step_offset = cur_offset;
	last_step_written = step_cursor;
	last_step_erase_ms = erase_ms;
	last_step_write_ms = write_ms;
	last_step_elapsed_ms = ms;
	last_step_valid = 1;

	/*
	 * Only the write pass finishing means the image is actually programmed;
	 * the erase pass finishing just lets the browser start the write pass.
	 */
	if (done && phase != 1)
		upgrade_success = 1;

	snprintf(resp_json, sizeof(resp_json),
		 "{\"status\":\"ok\",\"phase\":%d,\"written\":%u,\"total\":%u,\"done\":%u,"
		 "\"elapsed_ms\":%lu,\"erase_ms\":%u,\"write_ms\":%u}",
		 phase, step_cursor, step_total, done, ms, erase_ms, write_ms);

send:
	response->data = resp_json;
	response->size = strlen(resp_json);
}

static void style_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "style.css");
		response->info.content_type = "text/css";
	}
}

static void js_handler(enum httpd_uri_handler_status status,
		       struct httpd_request *request,
		       struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "main.js");
		response->info.content_type = "application/javascript";
	}
}

static void not_found_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "404.html");
		response->info.code = 404;
	}
}

static void html_handler(enum httpd_uri_handler_status status,
			 struct httpd_request *request,
			 struct httpd_response *response)
{
	if (status == HTTP_CB_RESPONDING || status == HTTP_CB_CLOSED) {
		if (request && request->urih &&
		    !strcmp(request->urih->uri, "/booting.html")) {
			upgrade_success = 1;
			mtk_tcp_reset_all_conn();
		}
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (output_plain_file(response, request->urih->uri + 1))
		not_found_handler(status, request, response);
}

static void reboot_handler(enum httpd_uri_handler_status status,
			   struct httpd_request *request,
			   struct httpd_response *response)
{
	if (status == HTTP_CB_RESPONDING || status == HTTP_CB_CLOSED) {
		reboot_requested = 1;
		mtk_tcp_reset_all_conn();
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json; charset=utf-8";
	response->data = "{\"status\":\"ok\",\"msg\":\"设备正在重启...\"}";
	response->size = strlen(response->data);
}

#if IS_ENABLED(CONFIG_USB_ETHER)
extern int zx29_usb_ether_register(void);
#endif

int start_web_failsafe(void)
{
	struct httpd_instance *inst;

	/* Set a default MAC address so GMAC probe doesn't abort */
	if (!env_get("ethaddr"))
		env_set("ethaddr", "02:29:75:20:00:01");

#if IS_ENABLED(CONFIG_USB_ETHER)
	zx29_usb_ether_register();
	env_set("ethact", "usb_ether");
	env_set("ethprime", "usb_ether");
	env_set("ethrotate", "no");
	env_set("cdc_connect_timeout", "60");

	/* Explicitly find and select usb_ether device */
	struct udevice *udev = eth_get_dev_by_name("usb_ether");
	if (udev) {
		eth_set_dev(udev);
	}
#endif

	inst = httpd_find_instance(80);
	if (inst)
		httpd_free_instance(inst);

	inst = httpd_create_instance(80);
	if (!inst) {
		printf("Error: failed to create HTTP server instance on port 80!\n");
		return -1;
	}

	/* Register HTTP endpoints */
	httpd_register_uri_handler(inst, "/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/index.html", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/flashing.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/fail.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/booting.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
	httpd_register_uri_handler(inst, "/main.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/flash_chunk", &flash_chunk_handler, NULL);
	httpd_register_uri_handler(inst, "/flash_step", &flash_step_handler, NULL);
	httpd_register_uri_handler(inst, "/version", &version_handler, NULL);
	httpd_register_uri_handler(inst, "/get_flash_info", &flash_info_handler, NULL);
	httpd_register_uri_handler(inst, "/set_flash_profile", &set_flash_profile_handler, NULL);
	httpd_register_uri_handler(inst, "/memtest", &memtest_handler, NULL);
	httpd_register_uri_handler(inst, "/backup", &backup_handler, NULL);
	httpd_register_uri_handler(inst, "/get_partitions", &get_partitions_handler, NULL);
	httpd_register_uri_handler(inst, "/get_bootargs", &get_bootargs_handler, NULL);
	httpd_register_uri_handler(inst, "/set_bootargs", &set_bootargs_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot", &reboot_handler, NULL);
	httpd_register_uri_handler(inst, "", &not_found_handler, NULL);

	printf("\n========================================================\n");
	printf("  ZX297520V3 Web Recovery Failsafe Server Running\n");
	printf("  Interface:    USB RNDIS Gadget + GMAC Ethernet\n");
	printf("  Open browser: http://%pI4/\n", &net_ip);
	printf("  Flash Model:  %s\n", zx_flash_get_name());
	printf("  Press Ctrl+C to abort\n");
	printf("========================================================\n\n");

	dhcpd_start();
	failsafe_httpd_running = 1;
	net_loop(MTK_TCP);
	failsafe_httpd_running = 0;
	dhcpd_stop();

	return 0;
}

static int do_httpd(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	/* Default static IP: 192.168.72.1 / 255.255.255.0 if not configured */
	if (net_ip.s_addr == 0) {
		net_ip = string_to_ip("192.168.72.1");
		net_netmask = string_to_ip("255.255.255.0");
	}

	upgrade_success = 0;
	reboot_requested = 0;
	ret = start_web_failsafe();

	if (upgrade_success || reboot_requested) {
		printf("\n[+] Rebooting device in 1s...\n");
		mdelay(1000);
		do_reset(NULL, 0, 0, NULL);
	}

	return ret;
}

U_BOOT_CMD(
	httpd, 1, 0, do_httpd,
	"Start ZX297520V3 Web Failsafe recovery HTTP server",
	""
);
