// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2009-2018  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner <mareklindner@neomailbox.ch>, Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 * License-Filename: LICENSES/preferred/GPL-2.0
 */

#include "netlink.h"
#include "main.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "bat-hosts.h"
#include "batadv_packet.h"
#include "batman_adv.h"
#include "netlink.h"
#include "functions.h"
#include "main.h"

struct nlquery_opts {
	int err;
};

struct nla_policy batadv_netlink_policy[NUM_BATADV_ATTR] = {
	[BATADV_ATTR_VERSION]			= { .type = NLA_STRING },
	[BATADV_ATTR_ALGO_NAME]			= { .type = NLA_STRING },
	[BATADV_ATTR_MESH_IFINDEX]		= { .type = NLA_U32 },
	[BATADV_ATTR_MESH_IFNAME]		= { .type = NLA_STRING,
						    .maxlen = IFNAMSIZ },
	[BATADV_ATTR_MESH_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_HARD_IFINDEX]		= { .type = NLA_U32 },
	[BATADV_ATTR_HARD_IFNAME]		= { .type = NLA_STRING,
						    .maxlen = IFNAMSIZ },
	[BATADV_ATTR_HARD_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_ORIG_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_TPMETER_RESULT]		= { .type = NLA_U8 },
	[BATADV_ATTR_TPMETER_TEST_TIME]		= { .type = NLA_U32 },
	[BATADV_ATTR_TPMETER_BYTES]		= { .type = NLA_U64 },
	[BATADV_ATTR_TPMETER_COOKIE]		= { .type = NLA_U32 },
	[BATADV_ATTR_PAD]			= { .type = NLA_UNSPEC },
	[BATADV_ATTR_ACTIVE]			= { .type = NLA_FLAG },
	[BATADV_ATTR_TT_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_TT_TTVN]			= { .type = NLA_U8 },
	[BATADV_ATTR_TT_LAST_TTVN]		= { .type = NLA_U8 },
	[BATADV_ATTR_TT_CRC32]			= { .type = NLA_U32 },
	[BATADV_ATTR_TT_VID]			= { .type = NLA_U16 },
	[BATADV_ATTR_TT_FLAGS]			= { .type = NLA_U32 },
	[BATADV_ATTR_FLAG_BEST]			= { .type = NLA_FLAG },
	[BATADV_ATTR_LAST_SEEN_MSECS]		= { .type = NLA_U32 },
	[BATADV_ATTR_NEIGH_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_TQ]			= { .type = NLA_U8 },
	[BATADV_ATTR_THROUGHPUT]		= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_UP]		= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_DOWN]		= { .type = NLA_U32 },
	[BATADV_ATTR_ROUTER]			= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_BLA_OWN]			= { .type = NLA_FLAG },
	[BATADV_ATTR_BLA_ADDRESS]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_BLA_VID]			= { .type = NLA_U16 },
	[BATADV_ATTR_BLA_BACKBONE]		= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_BLA_CRC]			= { .type = NLA_U16 },
	[BATADV_ATTR_DAT_CACHE_IP4ADDRESS]	= { .type = NLA_U32 },
	[BATADV_ATTR_DAT_CACHE_HWADDRESS]	= { .type = NLA_UNSPEC,
						    .minlen = ETH_ALEN,
						    .maxlen = ETH_ALEN },
	[BATADV_ATTR_DAT_CACHE_VID]		= { .type = NLA_U16 },
	[BATADV_ATTR_MCAST_FLAGS]		= { .type = NLA_U32 },
	[BATADV_ATTR_MCAST_FLAGS_PRIV]		= { .type = NLA_U32 },
};

int last_err;
char algo_name_buf[256] = "";
int64_t mcast_flags = -EOPNOTSUPP;
int64_t mcast_flags_priv = -EOPNOTSUPP;

int missing_mandatory_attrs(struct nlattr *attrs[], const int mandatory[],
			    int num)
{
	int i;

	for (i = 0; i < num; i++)
		if (!attrs[mandatory[i]])
			return -EINVAL;

	return 0;
}

static int print_error(struct sockaddr_nl *nla __maybe_unused,
		       struct nlmsgerr *nlerr,
		       void *arg __maybe_unused)
{
	if (nlerr->error != -EOPNOTSUPP)
		fprintf(stderr, "Error received: %s\n",
			strerror(-nlerr->error));

	last_err = nlerr->error;

	return NL_STOP;
}

static int stop_callback(struct nl_msg *msg, void *arg __maybe_unused)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	int *error = nlmsg_data(nlh);

	if (*error)
		fprintf(stderr, "Error received: %s\n", strerror(-*error));

	return NL_STOP;
}

static const int info_mandatory[] = {
	BATADV_ATTR_MESH_IFINDEX,
	BATADV_ATTR_MESH_IFNAME,
};

static const int info_hard_mandatory[] = {
	BATADV_ATTR_VERSION,
	BATADV_ATTR_ALGO_NAME,
	BATADV_ATTR_HARD_IFNAME,
	BATADV_ATTR_HARD_ADDRESS,
};

static int info_callback(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrs[BATADV_ATTR_MAX+1];
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct print_opts *opts = arg;
	const uint8_t *primary_mac;
	struct genlmsghdr *ghdr;
	const uint8_t *mesh_mac;
	const char *primary_if;
	const char *mesh_name;
	const char *version;
	char *extra_info = NULL;
	uint8_t ttvn = 0;
	uint16_t bla_group_id = 0;
	const char *algo_name;
	const char *extra_header;
	int ret;

	if (!genlmsg_valid_hdr(nlh, 0)) {
		fputs("Received invalid data from kernel.\n", stderr);
		exit(1);
	}

	ghdr = nlmsg_data(nlh);

	if (ghdr->cmd != BATADV_CMD_GET_MESH_INFO)
		return NL_OK;

	if (nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
		      genlmsg_len(ghdr), batadv_netlink_policy)) {
		fputs("Received invalid data from kernel.\n", stderr);
		exit(1);
	}

	if (missing_mandatory_attrs(attrs, info_mandatory,
				    ARRAY_SIZE(info_mandatory))) {
		fputs("Missing attributes from kernel\n", stderr);
		exit(1);
	}

	mesh_name = nla_get_string(attrs[BATADV_ATTR_MESH_IFNAME]);
	mesh_mac = nla_data(attrs[BATADV_ATTR_MESH_ADDRESS]);

	if (attrs[BATADV_ATTR_HARD_IFNAME]) {
		if (missing_mandatory_attrs(attrs, info_hard_mandatory,
					    ARRAY_SIZE(info_hard_mandatory))) {
			fputs("Missing attributes from kernel\n",
			      stderr);
			exit(1);
		}

		version = nla_get_string(attrs[BATADV_ATTR_VERSION]);
		algo_name = nla_get_string(attrs[BATADV_ATTR_ALGO_NAME]);
		primary_if = nla_get_string(attrs[BATADV_ATTR_HARD_IFNAME]);
		primary_mac = nla_data(attrs[BATADV_ATTR_HARD_ADDRESS]);

		snprintf(algo_name_buf, sizeof(algo_name_buf), "%s", algo_name);

		if (attrs[BATADV_ATTR_TT_TTVN])
			ttvn = nla_get_u8(attrs[BATADV_ATTR_TT_TTVN]);

		if (attrs[BATADV_ATTR_BLA_CRC])
			bla_group_id = nla_get_u16(attrs[BATADV_ATTR_BLA_CRC]);

		if (attrs[BATADV_ATTR_MCAST_FLAGS])
			mcast_flags = nla_get_u32(attrs[BATADV_ATTR_MCAST_FLAGS]);
		else
			mcast_flags = -EOPNOTSUPP;

		if (attrs[BATADV_ATTR_MCAST_FLAGS_PRIV])
			mcast_flags_priv = nla_get_u32(attrs[BATADV_ATTR_MCAST_FLAGS_PRIV]);
		else
			mcast_flags_priv = -EOPNOTSUPP;

		switch (opts->nl_cmd) {
		case BATADV_CMD_GET_TRANSTABLE_LOCAL:
			ret = asprintf(&extra_info, ", TTVN: %u", ttvn);
			if (ret < 0)
				extra_info = NULL;
			break;
		case BATADV_CMD_GET_BLA_BACKBONE:
		case BATADV_CMD_GET_BLA_CLAIM:
			ret = asprintf(&extra_info, ", group id: 0x%04x",
				       bla_group_id);
			if (ret < 0)
				extra_info = NULL;
			break;
		default:
			extra_info = strdup("");
			break;
		}

		if (opts->static_header)
			extra_header = opts->static_header;
		else
			extra_header = "";

		ret = asprintf(&opts->remaining_header,
			       "[B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%02x:%02x:%02x:%02x:%02x:%02x (%s/%02x:%02x:%02x:%02x:%02x:%02x %s)%s]\n%s",
			       version, primary_if,
			       primary_mac[0], primary_mac[1], primary_mac[2],
			       primary_mac[3], primary_mac[4], primary_mac[5],
			       mesh_name,
			       mesh_mac[0], mesh_mac[1], mesh_mac[2],
			       mesh_mac[3], mesh_mac[4], mesh_mac[5],
			       algo_name, extra_info, extra_header);
		if (ret < 0)
			opts->remaining_header = NULL;

		if (extra_info)
			free(extra_info);
	} else {
		ret = asprintf(&opts->remaining_header,
			       "BATMAN mesh %s disabled\n", mesh_name);
		if (ret < 0)
			opts->remaining_header = NULL;
	}

	return NL_STOP;
}

char *netlink_get_info(int ifindex, uint8_t nl_cmd, const char *header)
{
	struct nl_sock *sock;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int family;
	struct print_opts opts = {
		.read_opt = 0,
		.nl_cmd = nl_cmd,
		.remaining_header = NULL,
		.static_header = header,
	};

	sock = nl_socket_alloc();
	if (!sock)
		return NULL;

	genl_connect(sock);

	family = genl_ctrl_resolve(sock, BATADV_NL_NAME);
	if (family < 0) {
		nl_socket_free(sock);
		return NULL;
	}

	msg = nlmsg_alloc();
	if (!msg) {
		nl_socket_free(sock);
		return NULL;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, 0,
		    BATADV_CMD_GET_MESH_INFO, 1);

	nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, ifindex);

	nl_send_auto_complete(sock, msg);

	nlmsg_free(msg);

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
		goto err_free_sock;

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, info_callback, &opts);
	nl_cb_err(cb, NL_CB_CUSTOM, print_error, NULL);

	nl_recvmsgs(sock, cb);

err_free_sock:
	nl_socket_free(sock);

	return opts.remaining_header;
}

static void netlink_print_remaining_header(struct print_opts *opts)
{
	if (!opts->remaining_header)
		return;

	fputs(opts->remaining_header, stdout);
	free(opts->remaining_header);
	opts->remaining_header = NULL;
}

static int netlink_print_common_cb(struct nl_msg *msg, void *arg)
{
	struct print_opts *opts = arg;

	netlink_print_remaining_header(opts);

	return opts->callback(msg, arg);
}

static const int routing_algos_mandatory[] = {
	BATADV_ATTR_ALGO_NAME,
};

static int routing_algos_callback(struct nl_msg *msg, void *arg __maybe_unused)
{
	struct nlattr *attrs[BATADV_ATTR_MAX+1];
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct genlmsghdr *ghdr;
	const char *algo_name;

	if (!genlmsg_valid_hdr(nlh, 0)) {
		fputs("Received invalid data from kernel.\n", stderr);
		exit(1);
	}

	ghdr = nlmsg_data(nlh);

	if (ghdr->cmd != BATADV_CMD_GET_ROUTING_ALGOS)
		return NL_OK;

	if (nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
		      genlmsg_len(ghdr), batadv_netlink_policy)) {
		fputs("Received invalid data from kernel.\n", stderr);
		exit(1);
	}

	if (missing_mandatory_attrs(attrs, routing_algos_mandatory,
				    ARRAY_SIZE(routing_algos_mandatory))) {
		fputs("Missing attributes from kernel\n", stderr);
		exit(1);
	}

	algo_name = nla_get_string(attrs[BATADV_ATTR_ALGO_NAME]);

	printf(" * %s\n", algo_name);

	return NL_OK;
}

int netlink_print_routing_algos(void)
{
	struct nl_sock *sock;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int family;
	struct print_opts opts = {
		.callback = routing_algos_callback,
	};

	sock = nl_socket_alloc();
	if (!sock)
		return -ENOMEM;

	genl_connect(sock);

	family = genl_ctrl_resolve(sock, BATADV_NL_NAME);
	if (family < 0) {
		last_err = -EOPNOTSUPP;
		goto err_free_sock;
	}

	msg = nlmsg_alloc();
	if (!msg) {
		last_err = -ENOMEM;
		goto err_free_sock;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_DUMP,
		    BATADV_CMD_GET_ROUTING_ALGOS, 1);

	nl_send_auto_complete(sock, msg);

	nlmsg_free(msg);

	opts.remaining_header = strdup("Available routing algorithms:\n");

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		last_err = -ENOMEM;
		goto err_free_sock;
	}

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, netlink_print_common_cb,
		  &opts);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, stop_callback, NULL);
	nl_cb_err(cb, NL_CB_CUSTOM, print_error, NULL);

	nl_recvmsgs(sock, cb);

err_free_sock:
	nl_socket_free(sock);

	if (!last_err)
		netlink_print_remaining_header(&opts);

	return last_err;
}

int netlink_print_common(char *mesh_iface, char *orig_iface, int read_opt,
			 float orig_timeout, float watch_interval,
			 const char *header, uint8_t nl_cmd,
			 nl_recvmsg_msg_cb_t callback)
{
	struct print_opts opts = {
		.read_opt = read_opt,
		.orig_timeout = orig_timeout,
		.watch_interval = watch_interval,
		.remaining_header = NULL,
		.callback = callback,
	};
	int hardifindex = 0;
	struct nl_sock *sock;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ifindex;
	int family;

	sock = nl_socket_alloc();
	if (!sock)
		return -ENOMEM;

	genl_connect(sock);

	family = genl_ctrl_resolve(sock, BATADV_NL_NAME);
	if (family < 0) {
		last_err = -EOPNOTSUPP;
		goto err_free_sock;
	}

	ifindex = if_nametoindex(mesh_iface);
	if (!ifindex) {
		fprintf(stderr, "Interface %s is unknown\n", mesh_iface);
		last_err = -ENODEV;
		goto err_free_sock;
	}

	if (orig_iface) {
		hardifindex = if_nametoindex(orig_iface);
		if (!hardifindex) {
			fprintf(stderr, "Interface %s is unknown\n",
				orig_iface);
			last_err = -ENODEV;
			goto err_free_sock;
		}
	}

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		last_err = -ENOMEM;
		goto err_free_sock;
	}

	bat_hosts_init(read_opt);

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, netlink_print_common_cb, &opts);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, stop_callback, NULL);
	nl_cb_err(cb, NL_CB_CUSTOM, print_error, NULL);

	do {
		if (read_opt & CLR_CONT_READ)
			/* clear screen, set cursor back to 0,0 */
			printf("\033[2J\033[0;0f");

		if (!(read_opt & SKIP_HEADER))
			opts.remaining_header = netlink_get_info(ifindex,
								 nl_cmd,
								 header);

		msg = nlmsg_alloc();
		if (!msg)
			continue;

		genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0,
			    NLM_F_DUMP, nl_cmd, 1);

		nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, ifindex);
		if (hardifindex)
			nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX,
				    hardifindex);

		nl_send_auto_complete(sock, msg);

		nlmsg_free(msg);

		last_err = 0;
		nl_recvmsgs(sock, cb);

		/* the header should still be printed when no entry was received */
		if (!last_err)
			netlink_print_remaining_header(&opts);

		if (!last_err && read_opt & (CONT_READ|CLR_CONT_READ))
			usleep(1000000 * watch_interval);

	} while (!last_err && read_opt & (CONT_READ|CLR_CONT_READ));

	bat_hosts_free();

err_free_sock:
	nl_socket_free(sock);

	return last_err;
}

static int nlquery_error_cb(struct sockaddr_nl *nla __maybe_unused,
			    struct nlmsgerr *nlerr, void *arg)
{
	struct nlquery_opts *query_opts = arg;

	query_opts->err = nlerr->error;

	return NL_STOP;
}

static int nlquery_stop_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlquery_opts *query_opts = arg;
	int *error = nlmsg_data(nlh);

	if (*error)
		query_opts->err = *error;

	return NL_STOP;
}

static int netlink_query_common(const char *mesh_iface, uint8_t nl_cmd,
				nl_recvmsg_msg_cb_t callback, int flags,
				struct nlquery_opts *query_opts)
{
	struct nl_sock *sock;
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ifindex;
	int family;
	int ret;

	query_opts->err = 0;

	sock = nl_socket_alloc();
	if (!sock)
		return -ENOMEM;

	ret = genl_connect(sock);
	if (ret < 0) {
		query_opts->err = ret;
		goto err_free_sock;
	}

	family = genl_ctrl_resolve(sock, BATADV_NL_NAME);
	if (family < 0) {
		query_opts->err = -EOPNOTSUPP;
		goto err_free_sock;
	}

	ifindex = if_nametoindex(mesh_iface);
	if (!ifindex) {
		query_opts->err = -ENODEV;
		goto err_free_sock;
	}

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		query_opts->err = -ENOMEM;
		goto err_free_sock;
	}

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, callback, query_opts);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, nlquery_stop_cb, query_opts);
	nl_cb_err(cb, NL_CB_CUSTOM, nlquery_error_cb, query_opts);

	msg = nlmsg_alloc();
	if (!msg) {
		query_opts->err = -ENOMEM;
		goto err_free_cb;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, flags,
		    nl_cmd, 1);

	nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, ifindex);
	nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);

	nl_recvmsgs(sock, cb);

err_free_cb:
	nl_cb_put(cb);
err_free_sock:
	nl_socket_free(sock);

	return query_opts->err;
}

static const int translate_mac_netlink_mandatory[] = {
	BATADV_ATTR_TT_ADDRESS,
	BATADV_ATTR_ORIG_ADDRESS,
};

struct translate_mac_netlink_opts {
	struct ether_addr mac;
	bool found;
	struct nlquery_opts query_opts;
};

static int translate_mac_netlink_cb(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrs[BATADV_ATTR_MAX+1];
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlquery_opts *query_opts = arg;
	struct translate_mac_netlink_opts *opts;
	struct genlmsghdr *ghdr;
	uint8_t *addr;
	uint8_t *orig;

	opts = container_of(query_opts, struct translate_mac_netlink_opts,
			    query_opts);

	if (!genlmsg_valid_hdr(nlh, 0))
		return NL_OK;

	ghdr = nlmsg_data(nlh);

	if (ghdr->cmd != BATADV_CMD_GET_TRANSTABLE_GLOBAL)
		return NL_OK;

	if (nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
		      genlmsg_len(ghdr), batadv_netlink_policy)) {
		return NL_OK;
	}

	if (missing_mandatory_attrs(attrs, translate_mac_netlink_mandatory,
				    ARRAY_SIZE(translate_mac_netlink_mandatory)))
		return NL_OK;

	addr = nla_data(attrs[BATADV_ATTR_TT_ADDRESS]);
	orig = nla_data(attrs[BATADV_ATTR_ORIG_ADDRESS]);

	if (!attrs[BATADV_ATTR_FLAG_BEST])
		return NL_OK;

	if (memcmp(&opts->mac, addr, ETH_ALEN) != 0)
		return NL_OK;

	memcpy(&opts->mac, orig, ETH_ALEN);
	opts->found = true;
	opts->query_opts.err = 0;

	return NL_STOP;
}

int translate_mac_netlink(const char *mesh_iface, const struct ether_addr *mac,
			  struct ether_addr *mac_out)
{
	struct translate_mac_netlink_opts opts = {
		.found = false,
		.query_opts = {
			.err = 0,
		},
	};
	int ret;

	memcpy(&opts.mac, mac, ETH_ALEN);

	ret = netlink_query_common(mesh_iface,
				   BATADV_CMD_GET_TRANSTABLE_GLOBAL,
			           translate_mac_netlink_cb, NLM_F_DUMP,
				   &opts.query_opts);
	if (ret < 0)
		return ret;

	if (!opts.found)
		return -ENOENT;

	memcpy(mac_out, &opts.mac, ETH_ALEN);

	return 0;
}

static const int get_nexthop_netlink_mandatory[] = {
	BATADV_ATTR_ORIG_ADDRESS,
	BATADV_ATTR_NEIGH_ADDRESS,
	BATADV_ATTR_HARD_IFINDEX,
};

struct get_nexthop_netlink_opts {
	struct ether_addr mac;
	uint8_t *nexthop;
	char *ifname;
	bool found;
	struct nlquery_opts query_opts;
};

static int get_nexthop_netlink_cb(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrs[BATADV_ATTR_MAX+1];
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlquery_opts *query_opts = arg;
	struct get_nexthop_netlink_opts *opts;
	struct genlmsghdr *ghdr;
	const uint8_t *orig;
	const uint8_t *neigh;
	uint32_t index;
	const char *ifname;

	opts = container_of(query_opts, struct get_nexthop_netlink_opts,
			    query_opts);

	if (!genlmsg_valid_hdr(nlh, 0))
		return NL_OK;

	ghdr = nlmsg_data(nlh);

	if (ghdr->cmd != BATADV_CMD_GET_ORIGINATORS)
		return NL_OK;

	if (nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
		      genlmsg_len(ghdr), batadv_netlink_policy)) {
		return NL_OK;
	}

	if (missing_mandatory_attrs(attrs, get_nexthop_netlink_mandatory,
				    ARRAY_SIZE(get_nexthop_netlink_mandatory)))
		return NL_OK;

	orig = nla_data(attrs[BATADV_ATTR_ORIG_ADDRESS]);
	neigh = nla_data(attrs[BATADV_ATTR_NEIGH_ADDRESS]);
	index = nla_get_u32(attrs[BATADV_ATTR_HARD_IFINDEX]);

	if (!attrs[BATADV_ATTR_FLAG_BEST])
		return NL_OK;

	if (memcmp(&opts->mac, orig, ETH_ALEN) != 0)
		return NL_OK;

	/* save result */
	memcpy(opts->nexthop, neigh, ETH_ALEN);
	ifname = if_indextoname(index, opts->ifname);
	if (!ifname)
		return NL_OK;

	opts->found = true;
	opts->query_opts.err = 0;

	return NL_STOP;
}

int get_nexthop_netlink(const char *mesh_iface, const struct ether_addr *mac,
			uint8_t *nexthop, char *ifname)
{
	struct get_nexthop_netlink_opts opts = {
		.nexthop = 0,
		.found = false,
		.query_opts = {
			.err = 0,
		},
	};
	int ret;

	memcpy(&opts.mac, mac, ETH_ALEN);
	opts.nexthop = nexthop;
	opts.ifname = ifname;

	ret = netlink_query_common(mesh_iface,  BATADV_CMD_GET_ORIGINATORS,
			           get_nexthop_netlink_cb, NLM_F_DUMP,
				   &opts.query_opts);
	if (ret < 0)
		return ret;

	if (!opts.found)
		return -ENOENT;

	return 0;
}

static const int get_primarymac_netlink_mandatory[] = {
	BATADV_ATTR_HARD_ADDRESS,
};

struct get_primarymac_netlink_opts {
	uint8_t *primarymac;
	bool found;
	struct nlquery_opts query_opts;
};

static int get_primarymac_netlink_cb(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrs[BATADV_ATTR_MAX+1];
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlquery_opts *query_opts = arg;
	struct get_primarymac_netlink_opts *opts;
	struct genlmsghdr *ghdr;
	const uint8_t *primary_mac;

	opts = container_of(query_opts, struct get_primarymac_netlink_opts,
			    query_opts);

	if (!genlmsg_valid_hdr(nlh, 0))
		return NL_OK;

	ghdr = nlmsg_data(nlh);

	if (ghdr->cmd != BATADV_CMD_GET_MESH_INFO)
		return NL_OK;

	if (nla_parse(attrs, BATADV_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
		      genlmsg_len(ghdr), batadv_netlink_policy)) {
		return NL_OK;
	}

	if (missing_mandatory_attrs(attrs, get_primarymac_netlink_mandatory,
				    ARRAY_SIZE(get_primarymac_netlink_mandatory)))
		return NL_OK;

	primary_mac = nla_data(attrs[BATADV_ATTR_HARD_ADDRESS]);

	/* save result */
	memcpy(opts->primarymac, primary_mac, ETH_ALEN);

	opts->found = true;
	opts->query_opts.err = 0;

	return NL_STOP;
}

int get_primarymac_netlink(const char *mesh_iface, uint8_t *primarymac)
{
	struct get_primarymac_netlink_opts opts = {
		.primarymac = 0,
		.found = false,
		.query_opts = {
			.err = 0,
		},
	};
	int ret;

	opts.primarymac = primarymac;

	ret = netlink_query_common(mesh_iface, BATADV_CMD_GET_MESH_INFO,
			           get_primarymac_netlink_cb, 0,
				   &opts.query_opts);
	if (ret < 0)
		return ret;

	if (!opts.found)
		return -ENOENT;

	return 0;
}
