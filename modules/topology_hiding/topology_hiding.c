/**
 * Topology Hiding Module
 *
 * Copyright (C) 2015 OpenSIPS Foundation
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * History
 * -------
 *  2015-02-17  initial version (Vlad Paiu)
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "topo_hiding_logic.h"

struct tm_binds tm_api;
struct dlg_binds dlg_api;

int force_dialog = 0;
str topo_hiding_ct_params = {0,0};
str topo_hiding_ct_hdr_params = {0,0};
str topo_hiding_prefix = str_init("DLGCH_");
str topo_hiding_seed = str_init("OpenSIPS");
str topo_hiding_ct_encode_pw = str_init("ToPoCtPaSS");
str th_contact_encode_param = str_init("thinfo");
str th_contact_encode_scheme = str_init("base64");
str th_contact_caller_var = str_init("_th_contact_caller_username_var_");
str th_contact_callee_var = str_init("_th_contact_callee_username_var_");

int th_ct_enc_scheme;

static int mod_init(void);
static void mod_destroy(void);
static int fixup_mmode(void **param);
static int fixup_th_params(void **param);
int w_topology_hiding(struct sip_msg *req, str *flags_s, struct th_params *params);
int w_topology_hiding_match(struct sip_msg *req, void *seq_match_mode_val);
static int pv_topo_callee_callid(struct sip_msg *msg, pv_param_t *param, pv_value_t *res);

static const cmd_export_t cmds[]={
	{"topology_hiding",(cmd_function)w_topology_hiding, {
		{CMD_PARAM_STR|CMD_PARAM_OPT,0,0},
		{CMD_PARAM_STR|CMD_PARAM_OPT,fixup_th_params,fixup_free_pkg}, {0,0,0}},
		REQUEST_ROUTE},
	{"topology_hiding_match",(cmd_function)w_topology_hiding_match, {
		{CMD_PARAM_STR|CMD_PARAM_OPT, fixup_mmode, 0}, {0,0,0}},
		REQUEST_ROUTE},
	{0,0,{{0,0,0}},0}
};

/* Exported parameters */
static const param_export_t params[] = {
	{ "force_dialog",                INT_PARAM, &force_dialog                },
	{ "th_passed_contact_uri_params",STR_PARAM, &topo_hiding_ct_params.s     },
	{ "th_passed_contact_params",    STR_PARAM, &topo_hiding_ct_hdr_params.s },
	{ "th_callid_passwd",            STR_PARAM, &topo_hiding_seed.s          },
	{ "th_callid_prefix",            STR_PARAM, &topo_hiding_prefix.s        },
	{ "th_contact_encode_passwd",    STR_PARAM, &topo_hiding_ct_encode_pw.s  },
	{ "th_contact_encode_param",     STR_PARAM, &th_contact_encode_param.s   },
	{ "th_contact_encode_scheme",    STR_PARAM, &th_contact_encode_scheme.s  },
	{ "th_contact_caller_username_var", STR_PARAM, &th_contact_caller_var.s  },
	{ "th_contact_callee_username_var", STR_PARAM, &th_contact_callee_var.s  },
	{0, 0, 0}
};

static const pv_export_t pvars[] = {
	{ str_const_init("TH_callee_callid"), 1000,
		pv_topo_callee_callid,0,0, 0, 0, 0},
	{ {0, 0}, 0, 0, 0, 0, 0, 0, 0 }
};

static module_dependency_t *get_deps_dialog(const param_export_t *param)
{
	int force = *(int *)param->param_pointer;

	if (force == 0)
		return NULL;

	return alloc_module_dep(MOD_TYPE_DEFAULT, "dialog", DEP_ABORT);
}

static const dep_export_t deps = {
	{ /* OpenSIPS module dependencies */
		{ MOD_TYPE_DEFAULT, "tm", DEP_ABORT },
		{ MOD_TYPE_DEFAULT, "dialog", DEP_SILENT },
		{ MOD_TYPE_NULL, NULL, 0 },
	},
	{ /* modparam dependencies */
		{ "force_dialog",		get_deps_dialog },
		{ NULL, NULL },
	},
};

struct module_exports exports= {
	"topology_hiding",
	MOD_TYPE_DEFAULT, /* class of this module */
	MODULE_VERSION,
	DEFAULT_DLFLAGS,  /* dlopen flags */
	0,				  /* load function */
	&deps,            /* OpenSIPS module dependencies */
	cmds,             /* exported functions */
	0,                /* exported async functions */
	params,           /* param exports */
	0,                /* exported statistics */
	0,                /* exported MI functions */
	pvars,            /* exported pseudo-variables */
	0,				  /* exported transformations */
	0,                /* extra processes */
	0,                /* module pre-initialization function */
	mod_init,         /* module initialization function */
	(response_function) 0,
	mod_destroy,
	0,                /* per-child init function */
	0                 /* reload confirm function */
};

static int mod_init(void)
{
	LM_INFO("initializing...\n");

	/* param handling */
	topo_hiding_prefix.len = strlen(topo_hiding_prefix.s);
	topo_hiding_seed.len = strlen(topo_hiding_seed.s);
	th_contact_encode_param.len = strlen(th_contact_encode_param.s);
	topo_hiding_ct_encode_pw.len = strlen(topo_hiding_ct_encode_pw.s);
	if (topo_hiding_ct_params.s) {
		topo_hiding_ct_params.len = strlen(topo_hiding_ct_params.s);
		topo_parse_passed_ct_params(&topo_hiding_ct_params);
	}
	if (topo_hiding_ct_hdr_params.s) {
		topo_hiding_ct_hdr_params.len = strlen(topo_hiding_ct_hdr_params.s);
		topo_parse_passed_hdr_ct_params(&topo_hiding_ct_hdr_params);
	}
	th_contact_caller_var.len = strlen(th_contact_caller_var.s);
	th_contact_callee_var.len = strlen(th_contact_callee_var.s);
	th_contact_encode_scheme.len = strlen(th_contact_encode_scheme.s);
	if (!str_strcmp(&th_contact_encode_scheme, const_str("base64")))
		th_ct_enc_scheme = ENC_BASE64;
	else if (!str_strcmp(&th_contact_encode_scheme, const_str("base32")))
		th_ct_enc_scheme = ENC_BASE32;
	else {
		LM_ERR("Unsupported value for 'th_contact_encode_scheme' modparam!"
			"Use 'base64' or 'base32'\n");
		goto error;
	}


	/* loading dependencies */
	if (load_tm_api(&tm_api)!=0) {
		LM_ERR("can't load TM API\n");
		goto error;
	}

	if (load_dlg_api(&dlg_api)!=0) {
		if (force_dialog) {
			LM_ERR("cannot force dialog. dialog module not loaded\n");
			goto error;
		}
	}

	if (register_pre_raw_processing_cb(topo_callid_pre_raw, 
	PRE_RAW_PROCESSING, 0/*no free*/) < 0) {
		LM_ERR("failed to initialize pre raw support\n");
		return -1;
	}

	if (register_post_raw_processing_cb(topo_callid_post_raw,
	POST_RAW_PROCESSING, 0/*no free*/) < 0) {
		LM_ERR("failed to initialize post raw support\n");
		return -1;
	}
	/* restore dialog callbacks when restart */
	if (dlg_api.register_dlgcb && dlg_api.register_dlgcb(NULL,
				DLGCB_LOADED,th_loaded_callback, NULL, NULL) < 0)
			LM_ERR("cannot register callback for dialog loaded - topology "
					"hiding signalling for ongoing calls will be lost after "
					"restart\n");



	return 0;
error:
	return -1;
}

static void mod_destroy(void)
{
	return;
}

static int fixup_mmode(void **param)
{
	*param = (void*)(unsigned long)dlg_match_mode_str_to_int((str*)*param);

	return 0;
}

#define DIALOG_TH_PARAMS_SEP '/'

static int fixup_th_params(void **param)
{
	char *p;
	struct th_params *params = NULL;
	str *contacts = (str*)*param;
	str ct, caller;

	if (!contacts)
		return E_BUG;
	trim(contacts);

	if (!contacts->len) {
		*param = NULL;
		return 0;
	}

	params = pkg_malloc(sizeof *params + contacts->len);
	if (!params) {
		LM_ERR("oom for params\n");
		return E_OUT_OF_MEM;
	}
	memset(params, 0, sizeof *params);
	ct = *contacts;

	if (ct.s[0] == DIALOG_TH_PARAMS_SEP) {
		/* search for a second contact */
		ct.s++;
		ct.len--;
		caller = ct;
		p = q_memchr(ct.s, DIALOG_TH_PARAMS_SEP, ct.len);
		if (p) {
			/* we might have a callee as well; caller ends here */
			caller.s = ct.s;
			caller.len = p - ct.s;
			ct.len -= caller.len + 1;
			ct.s = p + 1;
			trim(&ct);
		} else {
			ct.len = 0;
		}

		trim(&caller);
		if (caller.len > 0) {
			params->ct_caller_user.s = (char *)(params + 1);
			params->ct_caller_user.len = caller.len;
			memcpy(params->ct_caller_user.s, caller.s, caller.len);
		}
		if (ct.len) {
			if (caller.len > 0)
				params->ct_callee_user.s = params->ct_caller_user.s + caller.len;
			else
				params->ct_callee_user.s = (char *)(params + 1);
			params->ct_callee_user.len = ct.len;
			memcpy(params->ct_callee_user.s, ct.s, ct.len);
		}
	} else {
		/* the contact is for both - copy it accordingly */
		params->ct_caller_user.s = params->ct_callee_user.s = (char *)(params + 1);
		params->ct_caller_user.len = params->ct_callee_user.len = ct.len;
		memcpy(params->ct_caller_user.s, ct.s, ct.len);
	}

	*param = params;
	return 0;
}

int w_topology_hiding(struct sip_msg *req, str *flags_s, struct th_params *params)
{
	int flags=0;
	char *p;

	if (flags_s)
		for (p=flags_s->s;p<flags_s->s+flags_s->len;p++)
		{
			switch (*p)
			{
				case 'U':
					flags |= TOPOH_KEEP_USER;
					LM_DBG("Will preserve usernames while doing topo hiding\n");
					break;
				case 'C':
					flags |= TOPOH_HIDE_CALLID;
					LM_DBG("Will change callid while doing topo hiding\n");
					break;
				case 'D':
					flags |= TOPOH_DID_IN_USER;
					LM_DBG("Will push DID into contact username\n");
					break;
				case 'a':
					flags |= TOPOH_KEEP_ADV_A;
					LM_DBG("Will store advertised contact for calller\n");
					break;
				case 'A':
					flags |= TOPOH_KEEP_ADV_B;
					LM_DBG("Will store advertised contact for calllee\n");
					break;
				default:
					LM_DBG("unknown topology_hiding flag : [%c] . Skipping\n",*p);
			}
		}

	return topology_hiding(req,flags,params);
}

int w_topology_hiding_match(struct sip_msg *req, void *seq_match_mode_val)
{
	int mm;

	/* copy-paste from w_match_dialog() */
	if (!seq_match_mode_val)
		mm = SEQ_MATCH_DEFAULT;
	else
		mm = (int)(long)seq_match_mode_val;

	if (!dlg_api.match_dialog || dlg_api.match_dialog(req, mm) < 0)
		return topology_hiding_match(req);
	else
		/* we went to the dlg module, which triggered us back, all good */
		return 1;
}

static char *callid_buf=NULL;
static int callid_buf_len=0;
static int pv_topo_callee_callid(struct sip_msg *msg, pv_param_t *param, pv_value_t *res)
{
	struct dlg_cell *dlg;
	int req_len = 0,i;

	if(res==NULL)
		return -1;

	if ( (dlg=dlg_api.get_dlg())==NULL || 
	(!dlg_api.is_mod_flag_set(dlg,TOPOH_HIDE_CALLID))) {
		return pv_get_null( msg, param, res);
	}


	req_len = calc_word64_encode_len(dlg->callid.len) + topo_hiding_prefix.len;

	if (req_len*2 > callid_buf_len) {
		callid_buf = pkg_realloc(callid_buf,req_len*2);
		if (callid_buf == NULL) {
			LM_ERR("No more pkg\n");
			return pv_get_null( msg, param, res);
		}

		callid_buf_len = req_len*2;
	}

	memcpy(callid_buf+req_len,topo_hiding_prefix.s,topo_hiding_prefix.len);
	for (i=0;i<dlg->callid.len;i++)
		callid_buf[i] = dlg->callid.s[i] ^ topo_hiding_seed.s[i%topo_hiding_seed.len];

	word64encode((unsigned char *)(callid_buf+topo_hiding_prefix.len+req_len),
		     (unsigned char *)(callid_buf),dlg->callid.len);

	res->rs.s = callid_buf+req_len;
	res->rs.len = req_len;
	res->flags = PV_VAL_STR;

	return 0;
}
