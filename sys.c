/*
 * Copyright (C) 2009-2016  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner <mareklindner@neomailbox.ch>
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
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "main.h"
#include "sys.h"
#include "functions.h"
#include "debug.h"

#define PATH_BUFF_LEN 200

const char *sysfs_param_enable[] = {
	"enable",
	"disable",
	"1",
	"0",
	NULL,
};

const char *sysfs_param_server[] = {
	"off",
	"client",
	"server",
	NULL,
};

const struct settings_data batctl_settings[BATCTL_SETTINGS_NUM] = {
	{
		.opt_long = "orig_interval",
		.opt_short = "it",
		.sysfs_name = "orig_interval",
		.params = NULL,
	},
	{
		.opt_long = "ap_isolation",
		.opt_short = "ap",
		.sysfs_name = "ap_isolation",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "bridge_loop_avoidance",
		.opt_short = "bl",
		.sysfs_name = "bridge_loop_avoidance",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "distributed_arp_table",
		.opt_short = "dat",
		.sysfs_name = "distributed_arp_table",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "aggregation",
		.opt_short = "ag",
		.sysfs_name = "aggregated_ogms",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "bonding",
		.opt_short = "b",
		.sysfs_name = "bonding",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "fragmentation",
		.opt_short = "f",
		.sysfs_name = "fragmentation",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "network_coding",
		.opt_short = "nc",
		.sysfs_name = "network_coding",
		.params = sysfs_param_enable,
	},
	{
		.opt_long = "isolation_mark",
		.opt_short = "mark",
		.sysfs_name = "isolation_mark",
		.params = NULL,
	},
	{
		.opt_long = "multicast_mode",
		.opt_short = "mm",
		.sysfs_name = "multicast_mode",
		.params = sysfs_param_enable,
	},
};

static void interface_usage(void)
{
	fprintf(stderr, "Usage: batctl [options] interface [parameters] [add|del iface(s)]\n");
	fprintf(stderr, "       batctl [options] interface [parameters] [create|destroy]\n");
	fprintf(stderr, "parameters:\n");
	fprintf(stderr, " \t -h print this help\n");
}

static struct nla_policy link_policy[IFLA_MAX + 1] = {
	[IFLA_IFNAME]           = { .type = NLA_STRING, .maxlen = IFNAMSIZ },
	[IFLA_MASTER]         = { .type = NLA_U32 },
};

struct print_interfaces_rtnl_arg {
	int ifindex;
};

static int print_interfaces_rtnl_parse(struct nl_msg *msg, void *arg)
{
	struct print_interfaces_rtnl_arg *print_arg = arg;
	struct nlattr *attrs[IFLA_MAX + 1];
	char path_buff[PATH_BUFF_LEN];
	struct ifinfomsg *ifm;
	char *ifname;
	int ret;
	const char *status;
	int master;

	ifm = nlmsg_data(nlmsg_hdr(msg));
	ret = nlmsg_parse(nlmsg_hdr(msg), sizeof(*ifm), attrs, IFLA_MAX,
			  link_policy);
	if (ret < 0)
		goto err;

	if (!attrs[IFLA_IFNAME])
		goto err;

	if (!attrs[IFLA_MASTER])
		goto err;

	ifname = nla_get_string(attrs[IFLA_IFNAME]);
	master = nla_get_u32(attrs[IFLA_MASTER]);

	/* required on older kernels which don't prefilter the results */
	if (master != print_arg->ifindex)
		goto err;

	snprintf(path_buff, sizeof(path_buff), SYS_IFACE_STATUS_FMT, ifname);
	ret = read_file("", path_buff, USE_READ_BUFF | SILENCE_ERRORS, 0, 0, 0);
	if (ret != EXIT_SUCCESS)
		status = "<error reading status>\n";
	else
		status = line_ptr;

	printf("%s: %s", ifname, status);

	free(line_ptr);
	line_ptr = NULL;

err:
	return NL_OK;
}

static int print_interfaces(char *mesh_iface)
{
	struct print_interfaces_rtnl_arg print_arg;

	if (!file_exists(module_ver_path)) {
		fprintf(stderr, "Error - batman-adv module has not been loaded\n");
		return EXIT_FAILURE;
	}

	print_arg.ifindex = if_nametoindex(mesh_iface);
	if (!print_arg.ifindex)
		return EXIT_FAILURE;

	query_rtnl_link(print_arg.ifindex, print_interfaces_rtnl_parse,
			&print_arg);

	return EXIT_SUCCESS;
}

struct count_interfaces_rtnl_arg {
	int ifindex;
	unsigned int count;
};

static int count_interfaces_rtnl_parse(struct nl_msg *msg, void *arg)
{
	struct count_interfaces_rtnl_arg *count_arg = arg;
	struct nlattr *attrs[IFLA_MAX + 1];
	struct ifinfomsg *ifm;
	int ret;
	int master;

	ifm = nlmsg_data(nlmsg_hdr(msg));
	ret = nlmsg_parse(nlmsg_hdr(msg), sizeof(*ifm), attrs, IFLA_MAX,
			  link_policy);
	if (ret < 0)
		goto err;

	if (!attrs[IFLA_IFNAME])
		goto err;

	if (!attrs[IFLA_MASTER])
		goto err;

	master = nla_get_u32(attrs[IFLA_MASTER]);

	/* required on older kernels which don't prefilter the results */
	if (master != count_arg->ifindex)
		goto err;

	count_arg->count++;

err:
	return NL_OK;
}

static unsigned int count_interfaces(char *mesh_iface)
{
	struct count_interfaces_rtnl_arg count_arg;

	count_arg.count = 0;
	count_arg.ifindex = if_nametoindex(mesh_iface);
	if (!count_arg.ifindex)
		return 0;

	query_rtnl_link(count_arg.ifindex, count_interfaces_rtnl_parse,
			&count_arg);

	return count_arg.count;
}

static int create_interface(const char *mesh_iface)
{
	struct ifinfomsg rt_hdr = {
		.ifi_family = IFLA_UNSPEC,
	};
	struct nlattr *linkinfo;
	struct nl_msg *msg;
	int err = 0;
	int ret;

	msg = nlmsg_alloc_simple(RTM_NEWLINK,
				 NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK);
	if (!msg) {
		return -ENOMEM;
	}

	ret = nlmsg_append(msg, &rt_hdr, sizeof(rt_hdr), NLMSG_ALIGNTO);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	ret = nla_put_string(msg, IFLA_IFNAME, mesh_iface);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	linkinfo = nla_nest_start(msg, IFLA_LINKINFO);
	if (!linkinfo) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	ret = nla_put_string(msg, IFLA_INFO_KIND, "batadv");
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	nla_nest_end(msg, linkinfo);

	err = netlink_simple_request(msg);

err_free_msg:
	nlmsg_free(msg);

	return err;
}

static int destroy_interface(const char *mesh_iface)
{
	struct ifinfomsg rt_hdr = {
		.ifi_family = IFLA_UNSPEC,
	};
	struct nl_msg *msg;
	int err = 0;
	int ret;

	msg = nlmsg_alloc_simple(RTM_DELLINK, NLM_F_REQUEST | NLM_F_ACK);
	if (!msg) {
		return -ENOMEM;
	}

	ret = nlmsg_append(msg, &rt_hdr, sizeof(rt_hdr), NLMSG_ALIGNTO);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	ret = nla_put_string(msg, IFLA_IFNAME, mesh_iface);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	err = netlink_simple_request(msg);

err_free_msg:
	nlmsg_free(msg);

	return err;
}

static int set_master_interface(const char *iface, unsigned int ifmaster)
{
	struct ifinfomsg rt_hdr = {
		.ifi_family = IFLA_UNSPEC,
	};
	struct nl_msg *msg;
	int err = 0;
	int ret;

	msg = nlmsg_alloc_simple(RTM_SETLINK, NLM_F_REQUEST | NLM_F_ACK);
	if (!msg) {
		return -ENOMEM;
	}

	ret = nlmsg_append(msg, &rt_hdr, sizeof(rt_hdr), NLMSG_ALIGNTO);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	ret = nla_put_string(msg, IFLA_IFNAME, iface);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	ret = nla_put_u32(msg, IFLA_MASTER, ifmaster);
	if (ret < 0) {
		err = -ENOMEM;
		goto err_free_msg;
	}

	err = netlink_simple_request(msg);

err_free_msg:
	nlmsg_free(msg);

	return err;
}

int interface(char *mesh_iface, int argc, char **argv)
{
	int i, optchar;
	int ret;
	unsigned int ifindex;
	unsigned int ifmaster;
	const char *long_op;
	unsigned int cnt;
	int rest_argc;
	char **rest_argv;

	while ((optchar = getopt(argc, argv, "h")) != -1) {
		switch (optchar) {
		case 'h':
			interface_usage();
			return EXIT_SUCCESS;
		default:
			interface_usage();
			return EXIT_FAILURE;
		}
	}

	rest_argc = argc - optind;
	rest_argv = &argv[optind];

	if (rest_argc == 0)
		return print_interfaces(mesh_iface);

	if ((strcmp(rest_argv[0], "add") != 0) && (strcmp(rest_argv[0], "a") != 0) &&
	    (strcmp(rest_argv[0], "del") != 0) && (strcmp(rest_argv[0], "d") != 0) &&
	    (strcmp(rest_argv[0], "create") != 0) && (strcmp(rest_argv[0], "c") != 0) &&
	    (strcmp(rest_argv[0], "destroy") != 0) && (strcmp(rest_argv[0], "D") != 0)) {
		fprintf(stderr, "Error - unknown argument specified: %s\n", rest_argv[0]);
		interface_usage();
		goto err;
	}

	if (strcmp(rest_argv[0], "destroy") == 0)
		rest_argv[0][0] = 'D';

	switch (rest_argv[0][0]) {
	case 'a':
	case 'd':
		if (rest_argc == 1) {
			fprintf(stderr,
				"Error - missing interface name(s) after '%s'\n",
				rest_argv[0]);
			interface_usage();
			goto err;
		}
		break;
	case 'c':
	case 'D':
		if (rest_argc != 1) {
			fprintf(stderr,
				"Error - extra parameter after '%s'\n",
				rest_argv[0]);
			interface_usage();
			goto err;
		}
		break;
	default:
		break;
	}

	switch (rest_argv[0][0]) {
	case 'c':
		ret = create_interface(mesh_iface);
		if (ret < 0) {
			fprintf(stderr,
				"Error - failed to create batman-adv interface: %s\n",
				strerror(-ret));
			goto err;
		}
		return EXIT_SUCCESS;
	case 'D':
		ret = destroy_interface(mesh_iface);
		if (ret < 0) {
			fprintf(stderr,
				"Error - failed to destroy batman-adv interface: %s\n",
				strerror(-ret));
			goto err;
		}
		return EXIT_SUCCESS;
	default:
		break;
	}

	/* get index of batman-adv interface - or try to create it */
	ifmaster = if_nametoindex(mesh_iface);
	if (!ifmaster && rest_argv[0][0] == 'a') {
		ret = create_interface(mesh_iface);
		if (ret < 0) {
			fprintf(stderr,
				"Error - failed to create batman-adv interface: %s\n",
				strerror(-ret));
			goto err;
		}

		ifmaster = if_nametoindex(mesh_iface);
	}

	if (!ifmaster) {
		ret = -ENODEV;
		fprintf(stderr,
			"Error - failed to find batman-adv interface: %s\n",
			strerror(-ret));
		goto err;
	}

	/* make sure that batman-adv is loaded or was loaded by create_interface */
	if (!file_exists(module_ver_path)) {
		fprintf(stderr, "Error - batman-adv module has not been loaded\n");
		goto err;
	}

	for (i = 1; i < rest_argc; i++) {
		ifindex = if_nametoindex(rest_argv[i]);

		if (!ifindex) {
			fprintf(stderr, "Error - interface does not exist: %s\n", rest_argv[i]);
			continue;
		}

		if (rest_argv[0][0] == 'a')
			ifindex = ifmaster;
		else
			ifindex = 0;

		ret = set_master_interface(rest_argv[i], ifindex);
		if (ret < 0) {
			if (rest_argv[0][0] == 'a')
				long_op = "add";
			else
				long_op = "delete";

			fprintf(stderr, "Error - failed to %s interface %s: %s\n",
				long_op, rest_argv[i], strerror(-ret));
			goto err;
		}
	}

	/* check if there is no interface left and then destroy mesh_iface */
	if (rest_argv[0][0] == 'd') {
		cnt = count_interfaces(mesh_iface);
		if (cnt == 0)
			destroy_interface(mesh_iface);
	}

	return EXIT_SUCCESS;

err:
	return EXIT_FAILURE;
}

static void log_level_usage(void)
{
	fprintf(stderr, "Usage: batctl [options] loglevel [parameters] [level[ level[ level]]...]\n");
	fprintf(stderr, "parameters:\n");
	fprintf(stderr, " \t -h print this help\n");
	fprintf(stderr, "levels:\n");
	fprintf(stderr, " \t none    Debug logging is disabled\n");
	fprintf(stderr, " \t all     Print messages from all below\n");
	fprintf(stderr, " \t batman  Messages related to routing / flooding / broadcasting\n");
	fprintf(stderr, " \t routes  Messages related to route added / changed / deleted\n");
	fprintf(stderr, " \t tt      Messages related to translation table operations\n");
	fprintf(stderr, " \t bla     Messages related to bridge loop avoidance\n");
	fprintf(stderr, " \t dat     Messages related to arp snooping and distributed arp table\n");
	fprintf(stderr, " \t nc      Messages related to network coding\n");
	fprintf(stderr, " \t mcast   Messages related to multicast\n");
}

int handle_loglevel(char *mesh_iface, int argc, char **argv)
{
	int optchar, res = EXIT_FAILURE;
	int log_level = 0;
	char *path_buff;
	char str[4];
	int i;

	while ((optchar = getopt(argc, argv, "h")) != -1) {
		switch (optchar) {
		case 'h':
			log_level_usage();
			return EXIT_SUCCESS;
		default:
			log_level_usage();
			return EXIT_FAILURE;
		}
	}

	path_buff = malloc(PATH_BUFF_LEN);
	snprintf(path_buff, PATH_BUFF_LEN, SYS_BATIF_PATH_FMT, mesh_iface);

	if (argc != 1) {
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "none") == 0) {
				log_level = 0;
				break;
			} else if (strcmp(argv[i], "all") == 0) {
				log_level = 63;
				break;
			} else if (strcmp(argv[i], "batman") == 0)
				log_level |= BIT(0);
			else if (strcmp(argv[i], "routes") == 0)
				log_level |= BIT(1);
			else if (strcmp(argv[i], "tt") == 0)
				log_level |= BIT(2);
			else if (strcmp(argv[i], "bla") == 0)
				log_level |= BIT(3);
			else if (strcmp(argv[i], "dat") == 0)
				log_level |= BIT(4);
			else if (strcmp(argv[i], "nc") == 0)
				log_level |= BIT(5);
			else if (strcmp(argv[i], "mcast") == 0)
				log_level |= BIT(6);
			else {
				log_level_usage();
				goto out;
			}
		}

		snprintf(str, sizeof(str), "%i", log_level);
		res = write_file(path_buff, SYS_LOG_LEVEL, str, NULL);
		goto out;
	}

	res = read_file(path_buff, SYS_LOG_LEVEL, USE_READ_BUFF, 0, 0, 0);

	if (res != EXIT_SUCCESS)
		goto out;

	log_level = strtol(line_ptr, (char **) NULL, 10);

	printf("[%c] %s (%s)\n", (!log_level) ? 'x' : ' ',
	       "all debug output disabled", "none");
	printf("[%c] %s (%s)\n", (log_level & BIT(0)) ? 'x' : ' ',
	       "messages related to routing / flooding / broadcasting",
	       "batman");
	printf("[%c] %s (%s)\n", (log_level & BIT(1)) ? 'x' : ' ',
	       "messages related to route added / changed / deleted", "routes");
	printf("[%c] %s (%s)\n", (log_level & BIT(2)) ? 'x' : ' ',
	       "messages related to translation table operations", "tt");
	printf("[%c] %s (%s)\n", (log_level & BIT(3)) ? 'x' : ' ',
	       "messages related to bridge loop avoidance", "bla");
	printf("[%c] %s (%s)\n", (log_level & BIT(4)) ? 'x' : ' ',
	       "messages related to arp snooping and distributed arp table", "dat");
	printf("[%c] %s (%s)\n", (log_level & BIT(5)) ? 'x' : ' ',
	       "messages related to network coding", "nc");
	printf("[%c] %s (%s)\n", (log_level & BIT(6)) ? 'x' : ' ',
	       "messages related to multicast", "mcast");

out:
	free(path_buff);
	return res;
}

static void settings_usage(int setting)
{
	fprintf(stderr, "Usage: batctl [options] %s|%s [parameters]",
	       (char *)batctl_settings[setting].opt_long, (char *)batctl_settings[setting].opt_short);

	if (batctl_settings[setting].params == sysfs_param_enable)
		fprintf(stderr, " [0|1]\n");
	else if (batctl_settings[setting].params == sysfs_param_server)
		fprintf(stderr, " [client|server]\n");
	else
		fprintf(stderr, "\n");

	fprintf(stderr, "parameters:\n");
	fprintf(stderr, " \t -h print this help\n");
}

int handle_sys_setting(char *mesh_iface, int setting, int argc, char **argv)
{
	int vid, optchar, res = EXIT_FAILURE;
	char *path_buff, *base_dev = NULL;
	const char **ptr;

	while ((optchar = getopt(argc, argv, "h")) != -1) {
		switch (optchar) {
		case 'h':
			settings_usage(setting);
			return EXIT_SUCCESS;
		default:
			settings_usage(setting);
			return EXIT_FAILURE;
		}
	}

	/* prepare the classic path */
	path_buff = malloc(PATH_BUFF_LEN);
	snprintf(path_buff, PATH_BUFF_LEN, SYS_BATIF_PATH_FMT, mesh_iface);

	/* if the specified interface is a VLAN then change the path to point
	 * to the proper "vlan%{vid}" subfolder in the sysfs tree.
	 */
	vid = vlan_get_link(mesh_iface, &base_dev);
	if (vid >= 0)
		snprintf(path_buff, PATH_BUFF_LEN, SYS_VLAN_PATH, base_dev, vid);

	if (argc == 1) {
		res = read_file(path_buff, (char *)batctl_settings[setting].sysfs_name,
				NO_FLAGS, 0, 0, 0);
		goto out;
	}

	if (!batctl_settings[setting].params)
		goto write_file;

	ptr = batctl_settings[setting].params;
	while (*ptr) {
		if (strcmp(*ptr, argv[1]) == 0)
			goto write_file;

		ptr++;
	}

	fprintf(stderr, "Error - the supplied argument is invalid: %s\n", argv[1]);
	fprintf(stderr, "The following values are allowed:\n");

	ptr = batctl_settings[setting].params;
	while (*ptr) {
		fprintf(stderr, " * %s\n", *ptr);
		ptr++;
	}

	goto out;

write_file:
	res = write_file(path_buff, (char *)batctl_settings[setting].sysfs_name,
			 argv[1], argc > 2 ? argv[2] : NULL);

out:
	free(path_buff);
	free(base_dev);
	return res;
}

static void gw_mode_usage(void)
{
	fprintf(stderr, "Usage: batctl [options] gw_mode [mode] [sel_class|bandwidth]\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, " \t -h print this help\n");
}

int handle_gw_setting(char *mesh_iface, int argc, char **argv)
{
	int optchar, res = EXIT_FAILURE;
	char *path_buff, gw_mode;
	const char **ptr;

	while ((optchar = getopt(argc, argv, "h")) != -1) {
		switch (optchar) {
		case 'h':
			gw_mode_usage();
			return EXIT_SUCCESS;
		default:
			gw_mode_usage();
			return EXIT_FAILURE;
		}
	}

	path_buff = malloc(PATH_BUFF_LEN);
	snprintf(path_buff, PATH_BUFF_LEN, SYS_BATIF_PATH_FMT, mesh_iface);

	if (argc == 1) {
		res = read_file(path_buff, SYS_GW_MODE, USE_READ_BUFF, 0, 0, 0);

		if (res != EXIT_SUCCESS)
			goto out;

		if (line_ptr[strlen(line_ptr) - 1] == '\n')
			line_ptr[strlen(line_ptr) - 1] = '\0';

		if (strcmp(line_ptr, "client") == 0)
			gw_mode = GW_MODE_CLIENT;
		else if (strcmp(line_ptr, "server") == 0)
			gw_mode = GW_MODE_SERVER;
		else
			gw_mode = GW_MODE_OFF;

		free(line_ptr);
		line_ptr = NULL;

		switch (gw_mode) {
		case GW_MODE_CLIENT:
			res = read_file(path_buff, SYS_GW_SEL, USE_READ_BUFF, 0, 0, 0);
			break;
		case GW_MODE_SERVER:
			res = read_file(path_buff, SYS_GW_BW, USE_READ_BUFF, 0, 0, 0);
			break;
		default:
			printf("off\n");
			goto out;
		}

		if (res != EXIT_SUCCESS)
			goto out;

		if (line_ptr[strlen(line_ptr) - 1] == '\n')
			line_ptr[strlen(line_ptr) - 1] = '\0';

		switch (gw_mode) {
		case GW_MODE_CLIENT:
			printf("client (selection class: %s)\n", line_ptr);
			break;
		case GW_MODE_SERVER:
			printf("server (announced bw: %s)\n", line_ptr);
			break;
		default:
			goto out;
		}

		free(line_ptr);
		line_ptr = NULL;
		goto out;
	}

	if (strcmp(argv[1], "client") == 0)
		gw_mode = GW_MODE_CLIENT;
	else if (strcmp(argv[1], "server") == 0)
		gw_mode = GW_MODE_SERVER;
	else if (strcmp(argv[1], "off") == 0)
		gw_mode = GW_MODE_OFF;
	else
		goto opt_err;

	res = write_file(path_buff, SYS_GW_MODE, argv[1], NULL);
	if (res != EXIT_SUCCESS)
		goto out;

	if (argc == 2)
		goto out;

	switch (gw_mode) {
	case GW_MODE_CLIENT:
		res = write_file(path_buff, SYS_GW_SEL, argv[2], NULL);
		break;
	case GW_MODE_SERVER:
		res = write_file(path_buff, SYS_GW_BW, argv[2], NULL);
		break;
	}

	goto out;

opt_err:
	fprintf(stderr, "Error - the supplied argument is invalid: %s\n", argv[1]);
	fprintf(stderr, "The following values are allowed:\n");

	ptr = sysfs_param_server;
	while (*ptr) {
		fprintf(stderr, " * %s\n", *ptr);
		ptr++;
	}

out:
	free(path_buff);
	return res;
}

static void ra_mode_usage(void)
{
	fprintf(stderr, "Usage: batctl [options] routing_algo [algorithm]\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, " \t -h print this help\n");
}

int handle_ra_setting(int argc, char **argv)
{
	DIR *iface_base_dir;
	struct dirent *iface_dir;
	int optchar;
	char *path_buff;
	int res = EXIT_FAILURE;
	int first_iface = 1;

	while ((optchar = getopt(argc, argv, "h")) != -1) {
		switch (optchar) {
		case 'h':
			ra_mode_usage();
			return EXIT_SUCCESS;
		default:
			ra_mode_usage();
			return EXIT_FAILURE;
		}
	}

	if (argc == 2) {
		res = write_file(SYS_SELECTED_RA_PATH, "", argv[1], NULL);
		goto out;
	}

	path_buff = malloc(PATH_BUFF_LEN);
	if (!path_buff) {
		fprintf(stderr, "Error - could not allocate path buffer: out of memory ?\n");
		goto out;
	}

	iface_base_dir = opendir(SYS_IFACE_PATH);
	if (!iface_base_dir) {
		fprintf(stderr, "Error - the directory '%s' could not be read: %s\n",
			SYS_IFACE_PATH, strerror(errno));
		fprintf(stderr, "Is the batman-adv module loaded and sysfs mounted ?\n");
		goto free_buff;
	}

	while ((iface_dir = readdir(iface_base_dir)) != NULL) {
		snprintf(path_buff, PATH_BUFF_LEN, SYS_ROUTING_ALGO_FMT, iface_dir->d_name);
		res = read_file("", path_buff, USE_READ_BUFF | SILENCE_ERRORS, 0, 0, 0);
		if (res != EXIT_SUCCESS)
			continue;

		if (line_ptr[strlen(line_ptr) - 1] == '\n')
			line_ptr[strlen(line_ptr) - 1] = '\0';

		if (first_iface) {
			first_iface = 0;
			printf("Active routing protocol configuration:\n");
		}

		printf(" * %s: %s\n", iface_dir->d_name, line_ptr);

		free(line_ptr);
		line_ptr = NULL;
	}

	closedir(iface_base_dir);
	free(path_buff);

	if (!first_iface)
		printf("\n");

	res = read_file("", SYS_SELECTED_RA_PATH, USE_READ_BUFF, 0, 0, 0);
	if (res != EXIT_SUCCESS)
		return EXIT_FAILURE;

	printf("Selected routing algorithm (used when next batX interface is created):\n");
	printf(" => %s\n", line_ptr);
	free(line_ptr);
	line_ptr = NULL;

	print_routing_algos();
	return EXIT_SUCCESS;

free_buff:
	free(path_buff);
out:
	return res;
}

int check_mesh_iface(char *mesh_iface)
{
	char *base_dev = NULL;
	char path_buff[PATH_BUFF_LEN];
	int ret = -1, vid;
	DIR *dir;

	/* use the parent interface if this is a VLAN */
	vid = vlan_get_link(mesh_iface, &base_dev);
	if (vid >= 0)
		snprintf(path_buff, PATH_BUFF_LEN, SYS_VLAN_PATH, base_dev, vid);
	else
		snprintf(path_buff, PATH_BUFF_LEN, SYS_BATIF_PATH_FMT, mesh_iface);

	/* try to open the mesh sys directory */
	dir = opendir(path_buff);
	if (!dir)
		goto out;

	closedir(dir);

	ret = 0;
out:
	if (base_dev)
		free(base_dev);

	return ret;
}

int check_mesh_iface_ownership(char *mesh_iface, char *hard_iface)
{
	char path_buff[PATH_BUFF_LEN];
	int res;

	/* check if this device actually belongs to the mesh interface */
	snprintf(path_buff, sizeof(path_buff), SYS_MESH_IFACE_FMT, hard_iface);
	res = read_file("", path_buff, USE_READ_BUFF | SILENCE_ERRORS, 0, 0, 0);
	if (res != EXIT_SUCCESS) {
		fprintf(stderr, "Error - the directory '%s' could not be read: %s\n",
			path_buff, strerror(errno));
		fprintf(stderr, "Is the batman-adv module loaded and sysfs mounted ?\n");
		return EXIT_FAILURE;
	}

	if (line_ptr[strlen(line_ptr) - 1] == '\n')
		line_ptr[strlen(line_ptr) - 1] = '\0';

	if (strcmp(line_ptr, mesh_iface) != 0) {
		fprintf(stderr, "Error - interface %s is part of batman network %s, not %s\n",
			hard_iface, line_ptr, mesh_iface);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
