// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Embedded Lightweight DHCPv4 Server for Zero-Config Web Recovery
 *
 * Hooks the U-Boot UDP receive handler to respond to DHCP DISCOVER and REQUEST
 * broadcasts over USB RNDIS, chaining non-DHCP UDP packets to the previous
 * handler and padding replies to the 300-byte RFC 2131 minimum.
 */

#include <command.h>
#include <env.h>
#include <net.h>
#include <linux/string.h>
#include <vsprintf.h>
#include "dhcpd.h"

#define OP_BOOTREQUEST              1
#define OP_BOOTREPLY                2
#define HWT_ETHER                   1
#define HWL_ETHER                   6

struct bootp_hdr {
	u8		bp_op;
	u8		bp_htype;
	u8		bp_hlen;
	u8		bp_hops;
	u32		bp_id;
	u16		bp_secs;
	u16		bp_spare1;	/* flags */
	struct in_addr	bp_ciaddr;
	struct in_addr	bp_yiaddr;
	struct in_addr	bp_siaddr;
	struct in_addr	bp_giaddr;
	u8		bp_chaddr[16];
	char		bp_sname[64];
	char		bp_file[128];
	char		bp_vend[312];
} __attribute__((packed));

#define DHCP_DISCOVER               1
#define DHCP_OFFER                  2
#define DHCP_REQUEST                3
#define DHCP_DECLINE                4
#define DHCP_ACK                    5
#define DHCP_NAK                    6
#define DHCP_RELEASE                7
#define DHCP_INFORM                 8

#define DHCP_OPTION_SUBNET_MASK     1
#define DHCP_OPTION_ROUTER          3
#define DHCP_OPTION_DNS             6
#define DHCP_OPTION_REQ_IPADDR      50
#define DHCP_OPTION_LEASE_TIME      51
#define DHCP_OPTION_MSG_TYPE        53
#define DHCP_OPTION_SERVER_ID       54
#define DHCP_OPTION_MESSAGE         56
#define DHCP_OPTION_END             255

/*
 * BOOTP 固定头(236) + magic cookie(4) 是能安全解析的最小长度。
 * 真实客户端的 DISCOVER/REQUEST 通常只有 ~300 字节，绝不会填满
 * 312 字节的 bp_vend，所以不能用 sizeof(struct bootp_hdr)(=548) 当门槛。
 */
#define BOOTP_FIXED_LEN		((int)(sizeof(struct bootp_hdr) - \
				sizeof(((struct bootp_hdr *)0)->bp_vend)))
#define DHCP_MIN_MSG_LEN	(BOOTP_FIXED_LEN + 4)

/* RFC 2131：DHCP 报文最小长度 300 字节，短于它的回复会被部分客户端丢弃 */
#define DHCP_MIN_REPLY_LEN	300

/* 地址池：server 网段的 .100 ~ .200，最多同时记录 16 条租约/冲突记录 */
#define DHCPD_POOL_START_HOST	100
#define DHCPD_POOL_SIZE		101
#define DHCPD_MAX_CLIENTS	16

#define BOOTP_FLAG_BROADCAST	0x8000

static const u8 dhcp_magic[4] = { 0x63, 0x82, 0x53, 0x63 };
static struct in_addr server_ip;
static struct in_addr netmask;
static struct in_addr gateway;
static struct in_addr dns_ip;

struct dhcpd_lease {
	bool	used;
	bool	declined;
	u8	mac[6];
	struct in_addr ip;
};
static struct dhcpd_lease leases[DHCPD_MAX_CLIENTS];

static rxhand_f *dhcpd_prev_handler;
static bool dhcpd_running;

/* ------------------------------------------------------------------ */
/* 小工具                                                             */
/* ------------------------------------------------------------------ */
static struct in_addr dhcpd_server_ip(void)
{
	if (net_ip.s_addr)
		return net_ip;
	return string_to_ip("192.168.72.1");
}

static struct in_addr dhcpd_netmask(void)
{
	if (net_netmask.s_addr)
		return net_netmask;
	return string_to_ip("255.255.255.0");
}

/* 池参数可用环境变量覆盖：dhcpd_pool_start / dhcpd_pool_size */
static u32 dhcpd_pool_start_host(void)
{
	const char *v = env_get("dhcpd_pool_start");

	return (v && v[0]) ? (u32)simple_strtoul(v, NULL, 0) :
			     DHCPD_POOL_START_HOST;
}

static u32 dhcpd_pool_size(void)
{
	const char *v = env_get("dhcpd_pool_size");

	return (v && v[0]) ? (u32)simple_strtoul(v, NULL, 0) :
			     DHCPD_POOL_SIZE;
}

/* 网段内 .start ~ .(start+size-1)，自动夹在网络地址范围内 */
static void dhcpd_pool_range(u32 *start, u32 *end)
{
	u32 ip_host = ntohl(server_ip.s_addr);
	u32 mask = ntohl(netmask.s_addr);
	u32 host_mask = ~mask;
	u32 net = ip_host & mask;
	u32 size = dhcpd_pool_size();
	u32 host_start = dhcpd_pool_start_host() & host_mask;
	u32 s, e;

	if (!size)
		size = 1;
	if (host_start == 0)
		host_start = 1;
	s = net | host_start;
	e = s + size - 1;
	if ((e & mask) != net || e > (net | host_mask))
		e = net | host_mask;
	if (start)
		*start = s;
	if (end)
		*end = e;
}

static struct dhcpd_lease *dhcpd_find_lease(const u8 *mac)
{
	int i;
	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (leases[i].used && !leases[i].declined &&
		    !memcmp(leases[i].mac, mac, 6))
			return &leases[i];
	}
	return NULL;
}

static void dhcpd_save_lease(const u8 *mac, struct in_addr ip)
{
	struct dhcpd_lease *l = dhcpd_find_lease(mac);
	int i;

	if (l) {
		l->declined = false;
		l->ip = ip;
		return;
	}
	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used) {
			leases[i].used = true;
			leases[i].declined = false;
			memcpy(leases[i].mac, mac, 6);
			leases[i].ip = ip;
			return;
		}
	}
	/* 表满：优先覆盖非 declined 项，否则覆盖第 0 条 */
	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].declined) {
			leases[i].used = true;
			leases[i].declined = false;
			memcpy(leases[i].mac, mac, 6);
			leases[i].ip = ip;
			return;
		}
	}
	leases[0].used = true;
	leases[0].declined = false;
	memcpy(leases[0].mac, mac, 6);
	leases[0].ip = ip;
}

static void dhcpd_mark_declined(const u8 *mac, struct in_addr bad_ip)
{
	int i;
	int free_slot = -1;

	/* 若客户端未带 Option 50 / ciaddr，回退查找该 MAC 当前租约 IP */
	if (!bad_ip.s_addr) {
		struct dhcpd_lease *l = dhcpd_find_lease(mac);
		if (l)
			bad_ip = l->ip;
	}
	if (!bad_ip.s_addr)
		return;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (leases[i].ip.s_addr == bad_ip.s_addr) {
			leases[i].declined = true;
			memset(leases[i].mac, 0xff, 6);
			return;
		}
	}

	if (free_slot < 0)
		free_slot = DHCPD_MAX_CLIENTS - 1;

	leases[free_slot].used = true;
	leases[free_slot].declined = true;
	memset(leases[free_slot].mac, 0xff, 6);
	leases[free_slot].ip = bad_ip;
}

/* 返回给该 MAC 分配的 IP（同 MAC 稳定复用，自动跳过被 DECLINE 的冲突地址） */
static struct in_addr dhcpd_alloc_ip(const u8 *mac)
{
	u32 start, end, i, pool_size, cand;
	struct dhcpd_lease *l = dhcpd_find_lease(mac);
	struct in_addr ip;
	int j;

	if (l) {
		u32 h = ntohl(l->ip.s_addr);
		dhcpd_pool_range(&start, &end);
		if (h >= start && h <= end)
			return l->ip;
	}

	dhcpd_pool_range(&start, &end);
	pool_size = (end >= start) ? (end - start + 1) : 0;
	for (i = 0; i < pool_size; i++) {
		cand = start + i;
		for (j = 0; j < DHCPD_MAX_CLIENTS; j++) {
			if (leases[j].used && ntohl(leases[j].ip.s_addr) == cand)
				break;
		}
		if (j == DHCPD_MAX_CLIENTS) {
			ip.s_addr = htonl(cand);
			return ip;
		}
	}
	ip.s_addr = htonl(start);	/* 池满：退回池首 */
	return ip;
}

static bool dhcpd_ip_in_pool(u32 ip_host)
{
	u32 start, end;
	dhcpd_pool_range(&start, &end);
	return ip_host >= start && ip_host <= end;
}

static bool dhcpd_ip_used_by_other(u32 ip_host, const u8 *mac)
{
	int i;
	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (!leases[i].used)
			continue;
		if (ntohl(leases[i].ip.s_addr) != ip_host)
			continue;
		if (leases[i].declined)
			return true;
		return memcmp(leases[i].mac, mac, 6) != 0;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* 选项解析（全部带边界检查）                                          */
/* ------------------------------------------------------------------ */
static u8 dhcpd_parse_msg_type(const u8 *pkt, unsigned len)
{
	const struct bootp_hdr *bp = (const struct bootp_hdr *)pkt;
	const u8 *opt, *end;
	u8 have_cookie;

	if (len < DHCP_MIN_MSG_LEN)
		return 0;
	opt = (const u8 *)bp->bp_vend;
	end = (const u8 *)pkt + len;
	if (memcmp(opt, dhcp_magic, 4) != 0)
		return 0;
	opt += 4;
	have_cookie = 1;
	(void)have_cookie;

	while (opt < end) {
		u8 code, olen;
		if (*opt == 0) { opt++; continue; }
		if (*opt == DHCP_OPTION_END) break;
		if (opt + 1 >= end) break;
		code = *opt++;
		olen = *opt++;
		if (opt + olen > end) break;
		if (code == DHCP_OPTION_MSG_TYPE && olen >= 1)
			return opt[0];
		opt += olen;
	}
	return 0;
}

static bool dhcpd_parse_opt_ip(const u8 *pkt, unsigned len, u8 want,
			       struct in_addr *out)
{
	const struct bootp_hdr *bp = (const struct bootp_hdr *)pkt;
	const u8 *opt, *end;

	if (len < DHCP_MIN_MSG_LEN)
		return false;
	opt = (const u8 *)bp->bp_vend;
	end = (const u8 *)pkt + len;
	if (memcmp(opt, dhcp_magic, 4) != 0)
		return false;
	opt += 4;

	while (opt < end) {
		u8 code, olen;
		if (*opt == 0) { opt++; continue; }
		if (*opt == DHCP_OPTION_END) break;
		if (opt + 1 >= end) break;
		code = *opt++;
		olen = *opt++;
		if (opt + olen > end) break;
		if (code == want && olen == 4) {
			memcpy(&out->s_addr, opt, 4);
			return true;
		}
		opt += olen;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* 回包                                                               */
/* ------------------------------------------------------------------ */
static u8 *dhcpd_opt_u8(u8 *p, u8 code, u8 val)
{
	*p++ = code;
	*p++ = 1;
	*p++ = val;
	return p;
}

static u8 *dhcpd_opt_ip(u8 *p, u8 code, struct in_addr addr)
{
	*p++ = code;
	*p++ = 4;
	memcpy(p, &addr.s_addr, 4);
	return p + 4;
}

static u8 *dhcpd_opt_u32be(u8 *p, u8 code, u32 be_val)
{
	*p++ = code;
	*p++ = 4;
	memcpy(p, &be_val, 4);
	return p + 4;
}

static void dhcpd_send_reply(const struct bootp_hdr *req, u8 msg_type,
			     struct in_addr yiaddr)
{
	struct in_addr bcast = { .s_addr = 0xffffffff };
	uchar *pkt = net_tx_packet;
	int eth_hdr_size;
	struct bootp_hdr *bp;
	u8 *p;
	int reply_len;

	/* 先占位建头，填完 payload 后再用真实长度重写 UDP 头 */
	eth_hdr_size = net_set_ether(pkt, net_bcast_ethaddr, PROT_IP);
	net_set_udp_header(pkt + eth_hdr_size, bcast, 68, 67, 0);

	bp = (struct bootp_hdr *)(pkt + eth_hdr_size + IP_UDP_HDR_SIZE);
	memset(bp, 0, sizeof(*bp));

	bp->bp_op = OP_BOOTREPLY;
	bp->bp_htype = HWT_ETHER;
	bp->bp_hlen = HWL_ETHER;
	bp->bp_hops = 0;
	bp->bp_id = req->bp_id;
	bp->bp_secs = req->bp_secs;
	/* 回复统一走广播，并把广播标志置上（部分客户端据此决定是否接受） */
	bp->bp_spare1 = htons(BOOTP_FLAG_BROADCAST);
	bp->bp_yiaddr = yiaddr;
	bp->bp_siaddr = server_ip;
	memcpy(bp->bp_chaddr, req->bp_chaddr, 16);

	p = (u8 *)bp->bp_vend;
	memcpy(p, dhcp_magic, 4);
	p += 4;

	p = dhcpd_opt_u8(p, DHCP_OPTION_MSG_TYPE, msg_type);
	p = dhcpd_opt_ip(p, DHCP_OPTION_SERVER_ID, server_ip);

	if (msg_type != DHCP_NAK) {
		p = dhcpd_opt_ip(p, DHCP_OPTION_SUBNET_MASK, netmask);
		p = dhcpd_opt_ip(p, DHCP_OPTION_ROUTER, gateway);
		p = dhcpd_opt_ip(p, DHCP_OPTION_DNS, dns_ip);
		p = dhcpd_opt_u32be(p, DHCP_OPTION_LEASE_TIME, htonl(3600));
	}
	*p++ = DHCP_OPTION_END;

	reply_len = (int)((uintptr_t)p - (uintptr_t)bp);
	if (reply_len < DHCP_MIN_REPLY_LEN)
		reply_len = DHCP_MIN_REPLY_LEN;	/* 余下已是 0（PAD） */

	net_set_udp_header(pkt + eth_hdr_size, bcast, 68, 67, reply_len);
	net_send_packet(pkt, eth_hdr_size + IP_UDP_HDR_SIZE + reply_len);

	printf("[DHCPD] tx %s: yiaddr=%pI4 server=%pI4 len=%d -> BROADCAST:68\n",
	       (msg_type == DHCP_OFFER) ? "OFFER" :
	       (msg_type == DHCP_ACK)   ? "ACK" : "NAK",
	       &yiaddr, &server_ip, reply_len);
}

/* ------------------------------------------------------------------ */
/* 收包处理                                                           */
/* ------------------------------------------------------------------ */
static void dhcpd_handle(uchar *pkt, unsigned dport, unsigned sport,
			 unsigned len)
{
	const struct bootp_hdr *req = (const struct bootp_hdr *)pkt;
	struct in_addr zero_ip = { .s_addr = 0 };
	struct in_addr server_id, req_ip;
	u8 msg_type;
	struct in_addr yiaddr;

	if (dport != 67 || sport != 68)
		return;

	if (len < DHCP_MIN_MSG_LEN) {
		printf("[DHCPD] drop: len=%u < %d\n", len, DHCP_MIN_MSG_LEN);
		return;
	}
	if (req->bp_op != OP_BOOTREQUEST) {
		printf("[DHCPD] drop: op=%u\n", req->bp_op);
		return;
	}
	if (req->bp_htype != HWT_ETHER || req->bp_hlen != HWL_ETHER)
		return;

	msg_type = dhcpd_parse_msg_type(pkt, len);
	if (!msg_type)
		return;

	printf("[DHCPD] rx %s from %02X:%02X:%02X:%02X:%02X:%02X (flags=0x%04X ciaddr=%pI4)\n",
	       (msg_type == DHCP_DISCOVER) ? "DISCOVER" :
	       (msg_type == DHCP_REQUEST)  ? "REQUEST"  :
	       (msg_type == DHCP_DECLINE)  ? "DECLINE"  :
	       (msg_type == DHCP_RELEASE)  ? "RELEASE"  :
	       (msg_type == DHCP_INFORM)   ? "INFORM"   : "other",
	       req->bp_chaddr[0], req->bp_chaddr[1], req->bp_chaddr[2],
	       req->bp_chaddr[3], req->bp_chaddr[4], req->bp_chaddr[5],
	       ntohs(req->bp_spare1), &req->bp_ciaddr);

	switch (msg_type) {
	case DHCP_DISCOVER:
		yiaddr = dhcpd_alloc_ip(req->bp_chaddr);
		/* 预登记给该 MAC，若随后客户端检测到冲突并发来 DECLINE 能准确定位 */
		dhcpd_save_lease(req->bp_chaddr, yiaddr);
		dhcpd_send_reply(req, DHCP_OFFER, yiaddr);
		break;

	case DHCP_REQUEST:
		/* 若带 option 54 且不是本服务器，忽略 */
		if (dhcpd_parse_opt_ip(pkt, len, DHCP_OPTION_SERVER_ID, &server_id) &&
		    server_id.s_addr != server_ip.s_addr)
			return;

		if (!dhcpd_parse_opt_ip(pkt, len, DHCP_OPTION_REQ_IPADDR, &req_ip)) {
			/* RENEWING/REBINDING 状态无 option 50，优先取客户端 ciaddr 校验 */
			if (req->bp_ciaddr.s_addr) {
				req_ip = req->bp_ciaddr;
			} else {
				yiaddr = dhcpd_alloc_ip(req->bp_chaddr);
				dhcpd_save_lease(req->bp_chaddr, yiaddr);
				dhcpd_send_reply(req, DHCP_ACK, yiaddr);
				break;
			}
		}

		if (!dhcpd_ip_in_pool(ntohl(req_ip.s_addr)) ||
		    dhcpd_ip_used_by_other(ntohl(req_ip.s_addr), req->bp_chaddr)) {
			printf("[DHCPD] REQUEST for %pI4 rejected -> NAK\n", &req_ip);
			dhcpd_send_reply(req, DHCP_NAK, zero_ip);
			break;
		}
		dhcpd_save_lease(req->bp_chaddr, req_ip);
		dhcpd_send_reply(req, DHCP_ACK, req_ip);
		break;

	case DHCP_DECLINE:
		/*
		 * 客户端检测到分配的 IP 已被占用（如本机其它网卡已持有同 IP 或 ARP 冲突）：
		 * 将该 IP 标记为 declined，下次 DISCOVER 自动顺延分配池中下一个地址。
		 */
		if (!dhcpd_parse_opt_ip(pkt, len, DHCP_OPTION_REQ_IPADDR, &req_ip))
			req_ip = req->bp_ciaddr;
		dhcpd_mark_declined(req->bp_chaddr, req_ip);
		printf("[DHCPD] DECLINE from %02X:%02X:%02X:%02X:%02X:%02X (bad_ip=%pI4) -> marked conflicted\n",
		       req->bp_chaddr[0], req->bp_chaddr[1], req->bp_chaddr[2],
		       req->bp_chaddr[3], req->bp_chaddr[4], req->bp_chaddr[5],
		       &req_ip);
		break;

	case DHCP_RELEASE: {
		struct dhcpd_lease *l = dhcpd_find_lease(req->bp_chaddr);
		if (l)
			l->used = false;
		break;
	}

	case DHCP_INFORM:
		dhcpd_send_reply(req, DHCP_ACK, req->bp_ciaddr);
		break;

	default:
		break;
	}
}

static void dhcpd_handler(uchar *pkt, unsigned dport, struct in_addr sip,
			  unsigned sport, unsigned len)
{
	dhcpd_handle(pkt, dport, sport, len);

	/* 链式调用：把包继续交给上一个 UDP handler，避免抢别人的包 */
	if (dhcpd_prev_handler && dhcpd_prev_handler != dhcpd_handler)
		dhcpd_prev_handler(pkt, dport, sip, sport, len);
}

/* ------------------------------------------------------------------ */
/* 启停                                                               */
/* ------------------------------------------------------------------ */
void dhcpd_start(void)
{
	u32 pool_s, pool_e;

	server_ip = dhcpd_server_ip();
	netmask = dhcpd_netmask();
	gateway = net_gateway.s_addr ? net_gateway : server_ip;
	dns_ip = net_dns_server.s_addr ? net_dns_server : server_ip;

	/* UDP 回复的源地址取自 net_ip，必须保证非 0，否则会是 0.0.0.0 */
	if (!net_ip.s_addr)
		net_ip = server_ip;
	if (!net_netmask.s_addr)
		net_netmask = netmask;
	if (!net_gateway.s_addr)
		net_gateway = gateway;
	if (!net_dns_server.s_addr)
		net_dns_server = dns_ip;

	/*
	 * net_init()/net_init_loop() 会 net_clear_handlers() 把 UDP handler 清掉。
	 * 如果已经启动但 handler 不是我们的了，就重新挂回去。
	 */
	if (dhcpd_running) {
		rxhand_f *cur = net_get_udp_handler();
		if (cur != dhcpd_handler) {
			dhcpd_prev_handler = cur;
			net_set_udp_handler(dhcpd_handler);
			printf("[DHCPD] re-hooked UDP handler\n");
		}
		return;
	}

	memset(leases, 0, sizeof(leases));
	dhcpd_prev_handler = net_get_udp_handler();
	if (dhcpd_prev_handler == dhcpd_handler)
		dhcpd_prev_handler = NULL;
	net_set_udp_handler(dhcpd_handler);
	dhcpd_running = true;

	dhcpd_pool_range(&pool_s, &pool_e);
	printf("[DHCPD] active: server=%pI4 mask=%pI4 gw=%pI4 pool=%pI4..%pI4\n",
	       &server_ip, &netmask, &gateway,
	       &(struct in_addr){ .s_addr = htonl(pool_s) },
	       &(struct in_addr){ .s_addr = htonl(pool_e) });
}

void dhcpd_stop(void)
{
	if (!dhcpd_running)
		return;
	/* 若已被网络栈清掉，就不要在这里“复活”出别的 handler */
	if (net_get_udp_handler() == dhcpd_handler)
		net_set_udp_handler(dhcpd_prev_handler);
	dhcpd_prev_handler = NULL;
	dhcpd_running = false;
}

/* ------------------------------------------------------------------ */
/* U-Boot 命令：dhcpd start / dhcpd stop（方便不开机也能手动验证）      */
/* ------------------------------------------------------------------ */
static int do_dhcpd(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "start")) {
		dhcpd_start();
		return CMD_RET_SUCCESS;
	}
	if (!strcmp(argv[1], "stop")) {
		dhcpd_stop();
		return CMD_RET_SUCCESS;
	}
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	dhcpd, 2, 0, do_dhcpd,
	"Control the built-in DHCP server",
	"start - start DHCP server (hook UDP 67)\n"
	"dhcpd stop  - stop it and restore the previous UDP handler\n"
	"\n"
	"Environment:\n"
	"  dhcpd_pool_start - first host index of the DHCP pool (default 100)\n"
	"  dhcpd_pool_size  - number of addresses in the pool (default 101)\n"
);
