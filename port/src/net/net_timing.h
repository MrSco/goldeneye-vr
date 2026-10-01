#ifndef GEVR_NET_TIMING_H
#define GEVR_NET_TIMING_H
#include <stdint.h>
#include <string.h>
#include <limits.h>
#define NET_CLOCK_FRESH_US 10000000ull
#define NET_CLOCK_MAX_RTT_US 1000000ull
#define NET_SHOT_MAX_AGE_US 500000ull
#define NET_SHOT_FUTURE_US 50000ull
#define NET_TRADE_WINDOW_US 150000ull
/* Four monotonic timestamps, measured independently of ENet's ping. Host owns
 * the offset estimate; no client-provided host-time estimate is trusted. */
typedef struct { uint64_t measured_us, rtt_us; int64_t offset_us; } NetClockSample;
typedef struct { NetClockSample samples[5]; unsigned cursor; } NetClockSync;
static inline int netClockSample(NetClockSync *c, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3) {
    if (!t0 || !t1 || t3<t0 || t2<t1 || t3-t0>NET_CLOCK_MAX_RTT_US || t2-t1>t3-t0
        || t0>INT64_MAX || t1>INT64_MAX || t2>INT64_MAX || t3>INT64_MAX) return 0;
    uint64_t rtt=(t3-t0)-(t2-t1);
    uint64_t host_mid=t0+(t3-t0)/2, client_mid=t1+(t2-t1)/2;
    c->samples[c->cursor++%5]=(NetClockSample){t3,rtt,(int64_t)host_mid-(int64_t)client_mid};
    return 1;
}
static inline const NetClockSample *netClockBest(const NetClockSync *c,uint64_t now) {
    const NetClockSample *best=0;
    for (int i=0;i<5;i++) {
        const NetClockSample *s=&c->samples[i];
        if (!s->measured_us || now<s->measured_us || now-s->measured_us>NET_CLOCK_FRESH_US) continue;
        if (!best || s->rtt_us<best->rtt_us || (s->rtt_us==best->rtt_us && s->measured_us>best->measured_us)) best=s;
    }
    return best;
}
static inline int netClockMap(const NetClockSync *c,uint64_t local,uint64_t now,uint64_t *host,unsigned *uncertainty) {
    const NetClockSample *s=netClockBest(c,now);
    if (!s || !local || local>INT64_MAX) return 0;
    if (s->offset_us>=0) {
        if (local>(uint64_t)INT64_MAX-(uint64_t)s->offset_us) return 0;
        *host=local+(uint64_t)s->offset_us;
    } else {
        uint64_t d=(uint64_t)(-s->offset_us);if(local<=d)return 0;*host=local-d;
    }
    /* Asymmetry is unknowable: never allow an unbounded tolerance. */
    if (uncertainty) *uncertainty=s->rtt_us/2>50000 ? 50000 : (unsigned)(s->rtt_us/2);
    return 1;
}
static inline int netShotTimeValid(uint64_t shot,uint64_t now,unsigned uncertainty) {
    if (!shot || shot>INT64_MAX || now>INT64_MAX) return 0;
    unsigned tolerance=uncertainty>50000 ? 50000 : uncertainty;
    return shot>now ? shot-now<=NET_SHOT_FUTURE_US+tolerance : now-shot<=NET_SHOT_MAX_AGE_US;
}
static inline int netShotTradeValid(uint64_t shot,uint64_t death,unsigned uncertainty) {
    if (!death) return 1;
    unsigned tolerance=uncertainty>25000 ? 25000 : uncertainty;
    return shot>death ? shot-death<=tolerance : death-shot<=NET_TRADE_WINDOW_US;
}
#endif
