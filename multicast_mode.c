// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2009-2018  B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing <linus.luessing@c0d3.blue>
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

#include "main.h"

#include <errno.h>
#include <linux/genetlink.h>
#include <netlink/genl/genl.h>

#include "batman_adv.h"
#include "netlink.h"
#include "sys.h"

static struct simple_boolean_data multicast_mode;

static int print_multicast_mode(struct nl_msg *msg, void *arg)
{
	return sys_simple_print_boolean(msg, arg, BATADV_ATTR_MULTICAST_MODE);
}

static int get_multicast_mode(struct state *state)
{
	return sys_simple_nlquery(state, BATADV_CMD_GET_MESH,
				  NULL, print_multicast_mode);
}

static int set_attrs_multicast_mode(struct nl_msg *msg, void *arg)
{
	struct state *state = arg;
	struct settings_data *settings = state->cmd->arg;
	struct simple_boolean_data *data = settings->data;

	nla_put_u8(msg, BATADV_ATTR_MULTICAST_MODE, data->val);

	return 0;
}

static int set_multicast_mode(struct state *state)
{
	return sys_simple_nlquery(state, BATADV_CMD_SET_MESH,
				  set_attrs_multicast_mode, NULL);
}

static struct settings_data batctl_settings_multicast_mode = {
	.sysfs_name = SYS_MULTICAST_MODE,
	.data = &multicast_mode,
	.parse = parse_simple_boolean,
	.netlink_get = get_multicast_mode,
	.netlink_set = set_multicast_mode,
};

COMMAND_NAMED(SUBCOMMAND, multicast_mode, "mm", handle_sys_setting,
	      COMMAND_FLAG_MESH_IFACE | COMMAND_FLAG_NETLINK,
	      &batctl_settings_multicast_mode,
	      "[0|1]             \tdisplay or modify multicast_mode setting");
