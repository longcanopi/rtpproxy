/* Auto-generated by genfincode_stat.sh - DO NOT EDIT! */
#if !defined(_rtpp_analyzer_fin_h)
#define _rtpp_analyzer_fin_h
#if !defined(RTPP_AUTOTRAP)
#define RTPP_AUTOTRAP() abort()
#else
extern int _naborts;
#endif
#if defined(RTPP_DEBUG)
void rtpp_analyzer_fin(struct rtpp_analyzer *);
#else
#define rtpp_analyzer_fin(arg) /* nop */
#endif
#if defined(RTPP_FINTEST)
void rtpp_analyzer_fintest(void);
#endif /* RTPP_FINTEST */
#endif /* _rtpp_analyzer_fin_h */
