/* Auto-generated by genfincode_stat.sh - DO NOT EDIT! */
#if !defined(_rtpp_refproxy_fin_h)
#define _rtpp_refproxy_fin_h
#if !defined(RTPP_AUTOTRAP)
#define RTPP_AUTOTRAP() abort()
#else
extern int _naborts;
#endif
#if defined(RTPP_DEBUG)
struct rtpp_refproxy;
void rtpp_refproxy_fin(struct rtpp_refproxy *);
#else
#define rtpp_refproxy_fin(arg) /* nop */
#endif
#if defined(RTPP_FINTEST)
void rtpp_refproxy_fintest(void);
#endif /* RTPP_FINTEST */
#endif /* _rtpp_refproxy_fin_h */