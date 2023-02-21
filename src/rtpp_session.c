/*
 * Copyright (c) 2006-2020 Sippy Software, Inc., http://www.sippysoft.com
 * Copyright (c) 2004-2006 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "rtpp_debug.h"
#include "rtpp_types.h"
#include "rtpp_log.h"
#include "rtpp_log_obj.h"
#include "rtpp_cfg.h"
#include "rtpp_acct_pipe.h"
#include "rtpp_acct.h"
#include "rtpp_analyzer.h"
#include "rtpp_command.h"
#include "rtpp_time.h"
#include "rtpp_command_args.h"
#include "rtpp_command_sub.h"
#include "rtpp_command_private.h"
#include "rtpp_genuid_singlet.h"
#include "rtpp_hash_table.h"
#include "rtpp_list.h"
#include "rtpp_mallocs.h"
#include "rtpp_module_if.h"
#include "rtpp_modman.h"
#include "rtpp_pipe.h"
#include "rtpp_stream.h"
#include "rtpp_session.h"
#include "rtpp_sessinfo.h"
#include "rtpp_stats.h"
#include "rtpp_ttl.h"
#include "rtpp_refcnt.h"
#include "rtpp_timeout_data.h"
#include "rtpp_proc_async.h"

struct rtpp_session_priv
{
    struct rtpp_session pub;
    struct rtpp_sessinfo *sessinfo;
    struct rtpp_modman *module_cf;
    struct rtpp_acct *acct;
};

static void rtpp_session_dtor(struct rtpp_session_priv *);

struct rtpp_session *
rtpp_session_ctor(const struct rtpp_cfg *cfs, struct common_cmd_args *ccap,
  const struct rtpp_timestamp *dtime, const struct sockaddr **lia, int weak,
  int lport, struct rtpp_socket **fds)
{
    struct rtpp_session_priv *pvt;
    struct rtpp_session *pub;
    struct rtpp_log *log;
    struct r_pipe_ctor_args pipe_cfg;
    int i;
    char *cp;

    pvt = rtpp_rzmalloc(sizeof(struct rtpp_session_priv), PVT_RCOFFS(pvt));
    if (pvt == NULL) {
        goto e0;
    }

    pub = &(pvt->pub);
    rtpp_gen_uid(&pub->seuid);

    log = rtpp_log_ctor("rtpproxy", ccap->call_id, 0);
    if (log == NULL) {
        goto e1;
    }
    CALL_METHOD(log, start, cfs);
    CALL_METHOD(log, setlevel, cfs->log_level);
    pipe_cfg = (struct r_pipe_ctor_args){.seuid = pub->seuid,
      .streams_wrt = cfs->rtp_streams_wrt, .servers_wrt = cfs->servers_wrt,
      .log = log, .rtpp_stats = cfs->rtpp_stats, .pipe_type = PIPE_RTP,
      .nmodules  = cfs->modules_cf->count.total,
      .pproc_manager = cfs->pproc_manager};
    pub->rtp = rtpp_pipe_ctor(&pipe_cfg);
    if (pub->rtp == NULL) {
        goto e2;
    }
    /* spb is RTCP twin session for this one. */
    pipe_cfg.streams_wrt = cfs->rtcp_streams_wrt;
    pipe_cfg.pipe_type = PIPE_RTCP;
    pub->rtcp = rtpp_pipe_ctor(&pipe_cfg);
    if (pub->rtcp == NULL) {
        goto e3;
    }
    pvt->acct = rtpp_acct_ctor(pub->seuid);
    if (pvt->acct == NULL) {
        goto e4;
    }
    pvt->acct->init_ts->wall = dtime->wall;
    pvt->acct->init_ts->mono = dtime->mono;
    pub->call_id = strdup(ccap->call_id);
    if (pub->call_id == NULL) {
        goto e5;
    }
    pub->tag = strdup(ccap->from_tag);
    if (pub->tag == NULL) {
        goto e6;
    }
    pub->tag_nomedianum = strdup(ccap->from_tag);
    if (pub->tag_nomedianum == NULL) {
        goto e7;
    }
    cp = strrchr(pub->tag_nomedianum, ';');
    if (cp != NULL)
        *cp = '\0';
    for (i = 0; i < 2; i++) {
        pub->rtp->stream[i]->laddr = lia[i];
        pub->rtcp->stream[i]->laddr = lia[i];
    }
    if (weak) {
        pub->rtp->stream[0]->weak = 1;
    } else {
        pub->strong = 1;
    }

    pub->rtp->stream[0]->port = lport;
    pub->rtcp->stream[0]->port = lport + 1;
    for (i = 0; i < 2; i++) {
        if (i == 0 || cfs->ttl_mode == TTL_INDEPENDENT) {
            pub->rtp->stream[i]->ttl = rtpp_ttl_ctor(cfs->max_setup_ttl);
            if (pub->rtp->stream[i]->ttl == NULL) {
                goto e8;
            }
        } else {
            pub->rtp->stream[i]->ttl = pub->rtp->stream[0]->ttl;
            RTPP_OBJ_INCREF(pub->rtp->stream[0]->ttl);
        }
        /* RTCP shares the same TTL */
        pub->rtcp->stream[i]->ttl = pub->rtp->stream[i]->ttl;
        RTPP_OBJ_INCREF(pub->rtp->stream[i]->ttl);
    }
    for (i = 0; i < 2; i++) {
        pub->rtp->stream[i]->stuid_rtcp = pub->rtcp->stream[i]->stuid;
        pub->rtcp->stream[i]->stuid_rtp = pub->rtp->stream[i]->stuid;
    }

    pvt->pub.rtpp_stats = cfs->rtpp_stats;
    pvt->pub.log = log;
    pvt->sessinfo = cfs->sessinfo;
    RTPP_OBJ_INCREF(cfs->sessinfo);
    if (cfs->modules_cf->count.sess_acct > 0) {
        RTPP_OBJ_INCREF(cfs->modules_cf);
        pvt->module_cf = cfs->modules_cf;
    }

    CALL_SMETHOD(cfs->sessinfo, append, pub, 0, fds);
    CALL_METHOD(cfs->rtpp_proc_cf, nudge);

    CALL_SMETHOD(pub->rcnt, attach, (rtpp_refcnt_dtor_t)&rtpp_session_dtor,
      pvt);
    return (&pvt->pub);

e8:
    free(pub->tag_nomedianum);
e7:
    free(pub->tag);
e6:
    free(pub->call_id);
e5:
    RTPP_OBJ_DECREF(pvt->acct);
e4:
    RTPP_OBJ_DECREF(pub->rtcp);
e3:
    RTPP_OBJ_DECREF(pub->rtp);
e2:
    RTPP_OBJ_DECREF(log);
e1:
    RTPP_OBJ_DECREF(pub);
    free(pvt);
e0:
    return (NULL);
}

static void
rtpp_session_dtor(struct rtpp_session_priv *pvt)
{
    int i;
    double session_time;
    struct rtpp_session *pub;

    pub = &(pvt->pub);
    rtpp_timestamp_get(pvt->acct->destroy_ts);
    session_time = pvt->acct->destroy_ts->mono - pvt->acct->init_ts->mono;

    CALL_SMETHOD(pub->rtp, get_stats, &pvt->acct->rtp);
    CALL_SMETHOD(pub->rtcp, get_stats, &pvt->acct->rtcp);
    if (pub->complete != 0) {
        CALL_SMETHOD(pub->rtp, upd_cntrs, &pvt->acct->rtp);
        CALL_SMETHOD(pub->rtcp, upd_cntrs, &pvt->acct->rtcp);
    }
    RTPP_LOG(pub->log, RTPP_LOG_INFO, "session on ports %d/%d is cleaned up",
      pub->rtp->stream[0]->port, pub->rtp->stream[1]->port);
    for (i = 0; i < 2; i++) {
        CALL_SMETHOD(pvt->sessinfo, remove, pub, i);
    }
    RTPP_OBJ_DECREF(pvt->sessinfo);
    CALL_SMETHOD(pub->rtpp_stats, updatebyname, "nsess_destroyed", 1);
    CALL_SMETHOD(pub->rtpp_stats, updatebyname_d, "total_duration",
      session_time);
    if (pvt->module_cf != NULL) {
        pvt->acct->call_id = pvt->pub.call_id;
        pvt->pub.call_id = NULL;
        pvt->acct->from_tag = pvt->pub.tag;
        pvt->pub.tag = NULL;
        CALL_SMETHOD(pub->rtp->stream[0]->analyzer, get_stats, \
          pvt->acct->rasto);
        CALL_SMETHOD(pub->rtp->stream[1]->analyzer, get_stats, \
          pvt->acct->rasta);
        CALL_SMETHOD(pub->rtp->stream[0]->analyzer, get_jstats, \
          pvt->acct->jrasto);
        CALL_SMETHOD(pub->rtp->stream[1]->analyzer, get_jstats, \
          pvt->acct->jrasta);

        CALL_METHOD(pvt->module_cf, do_acct, pvt->acct);
        RTPP_OBJ_DECREF(pvt->module_cf);
    }
    RTPP_OBJ_DECREF(pvt->acct);

    RTPP_OBJ_DECREF(pvt->pub.log);
    if (pvt->pub.timeout_data != NULL)
        RTPP_OBJ_DECREF(pvt->pub.timeout_data);
    if (pvt->pub.call_id != NULL)
        free(pvt->pub.call_id);
    if (pvt->pub.tag != NULL)
        free(pvt->pub.tag);
    if (pvt->pub.tag_nomedianum != NULL)
        free(pvt->pub.tag_nomedianum);

    RTPP_OBJ_DECREF(pvt->pub.rtcp);
    RTPP_OBJ_DECREF(pvt->pub.rtp);
    free(pvt);
}

int
compare_session_tags(const char *tag1, const char *tag0, unsigned *medianum_p)
{
    size_t len0 = strlen(tag0);

    if (!strncmp(tag1, tag0, len0)) {
	if (tag1[len0] == ';') {
	    if (medianum_p != NULL)
		*medianum_p = strtoul(tag1 + len0 + 1, NULL, 10);
	    return 2;
	}
	if (tag1[len0] == '\0')
	    return 1;
	return 0;
    }
    return 0;
}

struct session_match_args {
    const char *from_tag;
    const char *to_tag;
    struct rtpp_session *sp;
    int rval;
};

static int
rtpp_session_ematch(void *dp, void *ap)
{
    struct rtpp_session *rsp;
    struct session_match_args *map;
    const char *cp1, *cp2;

    rsp = (struct rtpp_session *)dp;
    map = (struct session_match_args *)ap;

    if (strcmp(rsp->tag, map->from_tag) == 0) {
        map->rval = 0;
        goto found;
    }
    if (map->to_tag != NULL) {
        switch (compare_session_tags(rsp->tag, map->to_tag, NULL)) {
        case 1:
            /* Exact tag match */
            map->rval = 1;
            goto found;

        case 2:
            /*
             * Reverse tag match without medianum. Medianum is always
             * applied to the from tag, verify that.
             */
            cp1 = strrchr(rsp->tag, ';');
            cp2 = strrchr(map->from_tag, ';');
            if (cp2 != NULL && strcmp(cp1, cp2) == 0) {
                map->rval = 1;
                goto found;
            }
            break;

        default:
            break;
        }
    }
    return (RTPP_HT_MATCH_CONT);

found:
    RTPP_OBJ_INCREF(rsp);
    RTPP_DBG_ASSERT(map->sp == NULL);
    map->sp = rsp;
    return (RTPP_HT_MATCH_BRK);
}

int
find_stream(const struct rtpp_cfg *cfsp, const char *call_id,
  const char *from_tag, const char *to_tag, struct rtpp_session **spp)
{
    struct session_match_args ma;

    memset(&ma, '\0', sizeof(ma));
    ma.from_tag = from_tag;
    ma.to_tag = to_tag;
    ma.rval = -1;

    CALL_SMETHOD(cfsp->sessions_ht, foreach_key, call_id,
      rtpp_session_ematch, &ma);
    if (ma.rval != -1) {
        *spp = ma.sp;
    }
    return ma.rval;
}
