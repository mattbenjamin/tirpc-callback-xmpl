/* packet-fbchan.c
 * Routines for fbchan (Duplex ONC RPC Example)  dissection
 * Copyright 2012, Linux Box Corporation.
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * Copied from WHATEVER_FILE_YOU_USED (where "WHATEVER_FILE_YOU_USED"
 * is a dissector file; if you just copied this from README.developer,
 * don't bother with the "Copied from" - you don't even need to put
 * in a "Copied from" if you copied an existing dissector, especially
 * if the bulk of the code in the new dissector is your code)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* Include only as needed */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <epan/packet.h>
#include <epan/prefs.h>

#include "packet-rpc.h"

#define FCHAN 0x20005001
#define BCHAN 0x20005002

/* Flag values and similar */
#define DUPLEX_UNIT_IMMED_CB 0x0001

/* RPC Programs */
static gint proto_fchan = -1;
static gint proto_bchan = -1;

/* header fields */
static gint hf_fchan_proc = -1;
static gint hf_fchan_sendmsg1 = -1;
static gint hf_fchan_msg = -1;
static gint hf_fchan_msg_seqnum = -1;
static gint hf_fchan_msg_msg1 = -1;
static gint hf_fchan_msg_msg2 = -1;
static gint hf_fchan_res = -1;
static gint hf_fchan_res_result = -1;
static gint hf_fchan_res_msg1 = -1;
static gint hf_fchan_read_args = -1;
static gint hf_fchan_read_args_seqnum = -1;
static gint hf_fchan_read_args_fileno = -1;
static gint hf_fchan_read_args_off = -1;
static gint hf_fchan_read_args_len = -1;
static gint hf_fchan_read_args_flags = -1;
static gint hf_fchan_read_args_flags2 = -1;
static gint hf_fchan_read_args_flags3 = -1;
static gint hf_fchan_read_args_flags4 = -1;
static gint hf_fchan_read_res = -1;
static gint hf_fchan_read_res_eof = -1;
static gint hf_fchan_read_res_flags = -1;
static gint hf_fchan_read_res_flags2 = -1;
static gint hf_fchan_read_res_flags3 = -1;
static gint hf_fchan_read_res_flags4 = -1;
static gint hf_fchan_read_res_data = -1;

static gint hf_fchan_write = -1;
static gint hf_fchan_write_args = -1;
static gint hf_fchan_write_args_seqnum = -1;
static gint hf_fchan_write_args_fileno = -1;
static gint hf_fchan_write_args_off = -1;
static gint hf_fchan_write_args_len = -1;
static gint hf_fchan_write_args_flags = -1;
static gint hf_fchan_write_args_flags2 = -1;
static gint hf_fchan_write_args_flags3 = -1;
static gint hf_fchan_write_args_flags4 = -1;
static gint hf_fchan_write_args_data = -1;

static gint hf_fchan_write_res = -1;
static gint hf_fchan_write_res_eof = -1;
static gint hf_fchan_write_res_flags = -1;
static gint hf_fchan_write_res_flags2 = -1;
static gint hf_fchan_write_res_flags3 = -1;
static gint hf_fchan_write_res_flags4 = -1;

static gint hf_bchan_proc = -1;
static gint hf_bchan_callback1 = -1;
static gint hf_bchan_msg = -1;
static gint hf_bchan_msg_seqnum = -1;
static gint hf_bchan_msg_msg1 = -1;
static gint hf_bchan_msg_msg2 = -1;
static gint hf_bchan_res = -1;
static gint hf_bchan_res_result = -1;
static gint hf_bchan_res_msg1 = -1;

/* ett */
static gint ett_fchan_proc = -1;
static gint ett_fchan_sendmsg1 = -1;
static gint ett_fchan_msg = -1;
static gint ett_fchan_res = -1;
static gint ett_fchan_read = -1;
static gint ett_fchan_read_args = -1;
static gint ett_fchan_read_res = -1;
static gint ett_fchan_write = -1;
static gint ett_fchan_write_args = -1;
static gint ett_fchan_write_res = -1;

static gint ett_bchan_proc = -1;
static gint ett_bchan_callback1 = -1;
static gint ett_bchan_msg = -1;
static gint ett_bchan_res = -1;

static int
dissect_fchan_sendmsg1_call(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                            proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_msg, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_msg);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_msg_seqnum, offset);
    offset = dissect_rpc_string(tvb, tree,
                                hf_fchan_msg_msg1, offset, NULL);
    offset = dissect_rpc_string(tvb, tree,
                                hf_fchan_msg_msg2, offset, NULL);

    return (offset);
}

static int
dissect_fchan_sendmsg1_reply(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                             proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_res, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_res);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_res_result, offset);
    offset = dissect_rpc_string(tvb, tree,
                                hf_fchan_res_msg1, offset, NULL);

    return (offset);
}

static int
dissect_fchan_bindconn1_call(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                             proto_tree *tree)
{
    /* Nothing here */
    return (offset);
}

static int
dissect_fchan_bindconn1_reply(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                              proto_tree *tree)
{
    /* Nothing here */
    return (offset);
}

static int
dissect_fchan_read_call(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                        proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_read_args, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_read_args);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_seqnum, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_fileno, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_off, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_len, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_flags, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_flags2, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_flags3, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_args_flags4, offset);

    return (offset);
}

static int
dissect_fchan_read_reply(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                         proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_read_res, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_read_res);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_res_eof, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_res_flags, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_res_flags2, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_res_flags3, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_read_res_flags4, offset);
    offset = dissect_rpc_data(tvb, a_tree,
                              hf_fchan_read_res_data, offset);

    return (offset);
}

static int
dissect_fchan_write_call(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                         proto_tree *tree)
{

    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_write_args, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_write_args);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_seqnum, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_fileno, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_off, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_len, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_flags, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_flags2, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_flags3, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_args_flags4, offset);
    offset = dissect_rpc_data(tvb, a_tree,
                              hf_fchan_write_args_data, offset);

    return (offset);
}

static int
dissect_fchan_write_reply(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                          proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_fchan_write_res, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_fchan_write_res);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_res_eof, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_res_flags, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_res_flags2, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_res_flags3, offset);
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_fchan_write_res_flags4, offset);

    return (offset);
}

static int
dissect_bchan_callback1_call(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                             proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_bchan_msg, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_bchan_msg);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_bchan_msg_seqnum, offset);
    offset = dissect_rpc_string(tvb, tree,
                                hf_bchan_msg_msg1, offset, NULL);
    offset = dissect_rpc_string(tvb, tree,
                                hf_bchan_msg_msg2, offset, NULL);

    return (offset);
}

static int
dissect_bchan_callback1_reply(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                              proto_tree *tree)
{
    proto_item *a_item = NULL;
    proto_tree *a_tree = NULL;

    a_item = proto_tree_add_item(tree, hf_bchan_res, tvb,
                                offset, -1, FALSE);

    a_tree = proto_item_add_subtree(a_item, ett_bchan_res);
    
    offset = dissect_rpc_uint32(tvb, a_tree,
                                hf_bchan_res_result, offset);
    offset = dissect_rpc_string(tvb, tree,
                                hf_bchan_res_msg1, offset, NULL);

    return (offset);
}

/* proc number, "proc name", dissect_request, dissect_reply */
/* NULL as function pointer means: type of arguments is "void". */
static const vsff fchan_proc[] = {
	{ 0,	"FCHAN_NULL",	NULL,	NULL },
	{ 1,	"FCHAN_SENDMSG1",
	dissect_fchan_sendmsg1_call,	dissect_fchan_sendmsg1_reply },
	{ 2,	"FCHAN_BIND_CONN_TO_SESSION1",
	dissect_fchan_bindconn1_call,	dissect_fchan_bindconn1_reply },
	{ 3,	"FCHAN_READ",
	dissect_fchan_read_call,	dissect_fchan_read_reply },
	{ 4,	"FCHAN_WRITE",
	dissect_fchan_write_call,	dissect_fchan_write_reply },
	{ 0,	NULL,	NULL,	NULL }
};

static const value_string fchan_proc_vals[] = {
	{ 0,	"FCHAN_NULL" },
	{ 1,	"FCHAN_SENDMSG1" },
	{ 2,	"FCHAN_BIND_CONN_TO_SESSION1" },
	{ 3,	"FCHAN_READ" },
	{ 4,	"FCHAN_WRITE" },
	{ 0,	NULL }
};

/* proc number, "proc name", dissect_request, dissect_reply */
/* NULL as function pointer means: type of arguments is "void". */
static const vsff bchan_proc[] = {
	{ 0,	"BCHAN_NULL",	NULL,	NULL },
	{ 1,	"BCHAN_CALLBACK1",
	dissect_bchan_callback1_call,	dissect_bchan_callback1_reply },
	{ 0,	NULL,	NULL,	NULL }
};

static const value_string bchan_proc_vals[] = {
	{ 0,	"BCHAN_NULL" },
	{ 1,	"BCHAN_CALLBACK1" },
	{ 0,	NULL }
};

void
proto_register_fbchan(void)
{
    static hf_register_info hf_fchan[] = {
        { &hf_fchan_proc, {
                "fchan", "fchan", FT_UINT32, BASE_DEC,
                VALS(fchan_proc_vals), 0, NULL, HFILL }},
        { &hf_fchan_msg, {
                "fchan_msg", "fchan_msg", FT_NONE, BASE_NONE,
                NULL, 0, "fchan_msg", HFILL }},
        { &hf_fchan_msg_seqnum, {
                "seqnum", "fchan_msg.seqnum", FT_UINT32, BASE_DEC,
                NULL, 0, "Sequence Number", HFILL }},
        { &hf_fchan_msg_msg1, {
                "msg1", "fchan_msg.msg1", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
        { &hf_fchan_msg_msg2, {
                "msg2", "fchan_msg.msg2", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
        { &hf_fchan_res, {
                "fchan_res", "fchan_res", FT_NONE, BASE_NONE,
                NULL, 0, "Result", HFILL }},
        { &hf_fchan_res_result, {
                "result", "fchan_res.result", FT_UINT32, BASE_DEC,
                NULL, 0, "Result", HFILL }},
        { &hf_fchan_res_msg1, {
                "msg1", "fchan_res.msg1", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
        { &hf_fchan_read_args, {
                "read_args", "read_args", FT_NONE, BASE_NONE,
                NULL, 0, "Read Args", HFILL }},
        { &hf_fchan_read_args_seqnum, {
                "seqnum", "read_args.seqnum", FT_UINT32, BASE_DEC,
                NULL, 0, "Sequence Number", HFILL }},
        { &hf_fchan_read_args_fileno, {
                "fileno", "read_args.fileno", FT_UINT32, BASE_DEC,
                NULL, 0, "File Number", HFILL }},
        { &hf_fchan_read_args_off, {
                "off", "read_args.off", FT_UINT32, BASE_DEC,
                NULL, 0, "Offset", HFILL }},
        { &hf_fchan_read_args_len, {
                "len", "read_args.len", FT_UINT32, BASE_DEC,
                NULL, 0, "Len", HFILL }},
        { &hf_fchan_read_args_flags, {
                "flags", "read_args.flags", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags", HFILL }},
        { &hf_fchan_read_args_flags2, {
                "flags2", "read_args.flags2", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags2", HFILL }},
        { &hf_fchan_read_args_flags3, {
                "flags3", "read_args.flags3", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags3", HFILL }},
        { &hf_fchan_read_args_flags4, {
                "flags4", "read_args.flags4", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags4", HFILL }},
        { &hf_fchan_read_res, {
                "read_res", "read_res", FT_NONE, BASE_NONE,
                NULL, 0, "Read Res", HFILL }},
        { &hf_fchan_read_res_eof, {
                "eof", "read_res.eof", FT_UINT32, BASE_DEC,
                NULL, 0, "Eof", HFILL }},
        { &hf_fchan_read_res_flags, {
                "flags", "read_res.flags", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags", HFILL }},
        { &hf_fchan_read_res_flags2, {
                "flags2", "read_res.flags2", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags2", HFILL }},
        { &hf_fchan_read_res_flags3, {
                "flags3", "read_res.flags3", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags3", HFILL }},
        { &hf_fchan_read_res_flags4, {
                "flags4", "read_res.flags4", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags4", HFILL }},
        { &hf_fchan_read_res_data, {
                "data", "read_res.data", FT_BYTES, BASE_NONE,
                NULL, 0, "Data", HFILL }},
        { &hf_fchan_write_args, {
                "write_args", "write_args", FT_NONE, BASE_NONE,
                NULL, 0, "Write Args", HFILL }},
        { &hf_fchan_write_args_seqnum, {
                "seqnum", "write_args.seqnum", FT_UINT32, BASE_DEC,
                NULL, 0, "Sequence Number", HFILL }},
        { &hf_fchan_write_args_fileno, {
                "fileno", "write_args.fileno", FT_UINT32, BASE_DEC,
                NULL, 0, "File Number", HFILL }},
        { &hf_fchan_write_args_off, {
                "off", "write_args.off", FT_UINT32, BASE_DEC,
                NULL, 0, "Offset", HFILL }},
        { &hf_fchan_write_args_len, {
                "len", "write_args.len", FT_UINT32, BASE_DEC,
                NULL, 0, "Len", HFILL }},
        { &hf_fchan_write_args_flags, {
                "flags", "write_args.flags", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags", HFILL }},
        { &hf_fchan_write_args_flags2, {
                "flags2", "write_args.flags2", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags2", HFILL }},
        { &hf_fchan_write_args_flags3, {
                "flags3", "write_args.flags3", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags3", HFILL }},
        { &hf_fchan_write_args_flags4, {
                "flags4", "write_args.flags4", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags4", HFILL }},
        { &hf_fchan_write_args_data, {
                "data", "write_args.data", FT_BYTES, BASE_NONE,
                NULL, 0, "Data", HFILL }},
        { &hf_fchan_write_res, {
                "write_res", "write_res", FT_NONE, BASE_NONE,
                NULL, 0, "Write Res", HFILL }},
        { &hf_fchan_write_res_eof, {
                "eof", "write_res.eof", FT_UINT32, BASE_DEC,
                NULL, 0, "Eof", HFILL }},
        { &hf_fchan_write_res_flags, {
                "flags", "write_res.flags", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags", HFILL }},
        { &hf_fchan_write_res_flags2, {
                "flags2", "write_res.flags2", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags2", HFILL }},
        { &hf_fchan_write_res_flags3, {
                "flags3", "write_res.flags3", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags3", HFILL }},
        { &hf_fchan_write_res_flags4, {
                "flags4", "write_res.flags4", FT_UINT32, BASE_HEX,
                NULL, 0, "Flags4", HFILL }},
    };

    static hf_register_info hf_bchan[] = {
        { &hf_bchan_proc, {
                "bchan", "bchan", FT_UINT32, BASE_DEC,
                VALS(bchan_proc_vals), 0, NULL, HFILL }},
        { &hf_bchan_msg, {
                "bchan_msg", "bchan_msg", FT_NONE, BASE_NONE,
                NULL, 0, "Bchan Msg", HFILL }},
        { &hf_bchan_msg_seqnum, {
                "seqnum", "bchan_msg.seqnum", FT_UINT32, BASE_DEC,
                NULL, 0, "Sequence Number", HFILL }},
        { &hf_bchan_msg_msg1, {
                "msg1", "bchan_msg.msg1", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
        { &hf_bchan_msg_msg2, {
                "msg1", "bchan_msg.msg2", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
        { &hf_bchan_res, {
                "bchan_res", "bchan_res", FT_NONE, BASE_NONE,
                NULL, 0, "Bchan Res", HFILL }},
        { &hf_bchan_res_result, {
                "result", "bchan_res.result", FT_UINT32, BASE_DEC,
                NULL, 0, "Sequence Number", HFILL }},
        { &hf_bchan_res_msg1, {
                "msg1", "bchan_res.msg1", FT_STRING, BASE_NONE,
                NULL, 0, "Message 1", HFILL }},
    };

    static gint *ett_fchan[] = {
        &ett_fchan_proc,
        &ett_fchan_sendmsg1,
        &ett_fchan_msg,
        &ett_fchan_res,
        &ett_fchan_read,
        &ett_fchan_read_args,
        &ett_fchan_read_res,
        &ett_fchan_write,
        &ett_fchan_write_args,
        &ett_fchan_write_res,
    };

    static gint *ett_bchan[] = {
        &ett_bchan_proc,
        &ett_bchan_callback1,
        &ett_bchan_msg,
        &ett_bchan_res,
    };

    proto_fchan = proto_register_protocol(
        "Duplex RPC Test FCHAN",
        "fchan",
        "fchan");

    proto_register_field_array(proto_fchan, hf_fchan, array_length(hf_fchan));
    proto_register_subtree_array(ett_fchan, array_length(ett_fchan));

    proto_bchan = proto_register_protocol(
        "Duplex RPC Test BCHAN",
        "bchan",
        "bchan");

    proto_register_field_array(proto_bchan, hf_bchan, array_length(hf_bchan));
    proto_register_subtree_array(ett_bchan, array_length(ett_bchan));
}

void
proto_reg_handoff_fbchan(void)
{
    /* Register the protocol as RPC */
    rpc_init_prog(proto_fchan, FCHAN, ett_fchan_proc);
    rpc_init_prog(proto_bchan, BCHAN, ett_bchan_proc);

    /* Register the procedure tables */
    rpc_init_proc_table(FCHAN, 1 /* vers */, fchan_proc, hf_fchan_proc);
    rpc_init_proc_table(BCHAN, 1 /* vers */, bchan_proc, hf_bchan_proc);
}
