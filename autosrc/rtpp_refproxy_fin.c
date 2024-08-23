/* Auto-generated by genfincode_stat.sh - DO NOT EDIT! */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define RTPP_FINCODE
#include "rtpp_types.h"
#include "rtpp_debug.h"
#include "rtpp_refproxy.h"
#include "rtpp_refproxy_fin.h"
static void rtpp_refproxy_add_fin(void *pub) {
    fprintf(stderr, "Method rtpp_refproxy@%p::add (rtpp_refproxy_add) is invoked after destruction\x0a", pub);
    RTPP_AUTOTRAP();
}
static const struct rtpp_refproxy_smethods rtpp_refproxy_smethods_fin = {
    .add = (rtpp_refproxy_add_t)&rtpp_refproxy_add_fin,
};
void rtpp_refproxy_fin(struct rtpp_refproxy *pub) {
    RTPP_DBG_ASSERT(pub->smethods->add != (rtpp_refproxy_add_t)NULL);
    RTPP_DBG_ASSERT(pub->smethods != &rtpp_refproxy_smethods_fin &&
      pub->smethods != NULL);
    pub->smethods = &rtpp_refproxy_smethods_fin;
}
#if defined(RTPP_FINTEST)
#include <assert.h>
#include <stddef.h>
#include "rtpp_mallocs.h"
#include "rtpp_refcnt.h"
#include "rtpp_linker_set.h"
#define CALL_TFIN(pub, fn) ((void (*)(typeof(pub)))((pub)->smethods->fn))(pub)

void
rtpp_refproxy_fintest()
{
    int naborts_s;

    struct {
        struct rtpp_refproxy pub;
    } *tp;

    naborts_s = _naborts;
    tp = rtpp_rzmalloc(sizeof(*tp), offsetof(typeof(*tp), pub.rcnt));
    assert(tp != NULL);
    assert(tp->pub.rcnt != NULL);
    static const struct rtpp_refproxy_smethods dummy = {
        .add = (rtpp_refproxy_add_t)((void *)0x1),
    };
    tp->pub.smethods = &dummy;
    CALL_SMETHOD(tp->pub.rcnt, attach, (rtpp_refcnt_dtor_t)&rtpp_refproxy_fin,
      &tp->pub);
    RTPP_OBJ_DECREF(&(tp->pub));
    CALL_TFIN(&tp->pub, add);
    assert((_naborts - naborts_s) == 1);
    free(tp);
}
const static void *_rtpp_refproxy_ftp = (void *)&rtpp_refproxy_fintest;
DATA_SET(rtpp_fintests, _rtpp_refproxy_ftp);
#endif /* RTPP_FINTEST */
