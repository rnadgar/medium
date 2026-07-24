/*
 * battery.c — 4S LiFeS2 voltage -> percent curves. NO ESP-IDF INCLUDES.
 *
 * Curve sources: Energizer L91 datasheet discharge curves at ~100-250 mA
 * (loaded) and low-rate (unloaded) drain, x4 cells in series, scaled to a
 * 4.0 V pack cutoff (1.0 V/cell). Design estimates — refine at bring-up
 * against docs/power-budget.md measurement plan (M2).
 */
#include "battery.h"

#include <stddef.h>

typedef struct {
    uint16_t mv;      /* pack voltage */
    uint8_t percent;  /* remaining capacity */
} batt_point_t;

/* Table entries MUST be strictly decreasing in mv. Percent is
 * non-increasing. Interpolation is linear between adjacent points. */
static const batt_point_t k_loaded_curve[] = {
    { 6400, 100 }, /* fresh pack under load */
    { 6000, 95 },
    { 5800, 85 },
    { 5600, 70 },
    { 5400, 55 },
    { 5200, 40 },
    { 5000, 25 },
    { 4700, 10 },
    { 4400, 3 },
    { 4000, 0 },   /* pack cutoff: 1.0 V/cell under load */
};

/* Unloaded curve sits higher and flatter — only the tails carry signal. */
static const batt_point_t k_unloaded_curve[] = {
    { 7000, 100 }, /* fresh open-circuit ~1.75 V/cell */
    { 6400, 90 },
    { 6100, 70 },
    { 5900, 45 },
    { 5700, 25 },
    { 5400, 10 },
    { 5000, 3 },
    { 4400, 0 },
};

static uint8_t interp_percent(const batt_point_t *curve, size_t n,
                              uint32_t pack_mv)
{
    if (n == 0) {
        return 0;
    }
    if (pack_mv >= curve[0].mv) {
        return curve[0].percent;
    }
    if (pack_mv <= curve[n - 1].mv) {
        return curve[n - 1].percent;
    }
    for (size_t i = 1; i < n; i++) {
        if (pack_mv >= curve[i].mv) {
            const batt_point_t *hi = &curve[i - 1];
            const batt_point_t *lo = &curve[i];
            uint32_t span_mv = hi->mv - lo->mv;
            uint32_t span_pct = hi->percent - lo->percent;
            uint32_t off_mv = pack_mv - lo->mv;
            /* Round to nearest percent. */
            return (uint8_t)(lo->percent +
                             (off_mv * span_pct + span_mv / 2) / span_mv);
        }
    }
    return curve[n - 1].percent; /* unreachable */
}

uint8_t battery_percent_from_loaded_mv(uint32_t pack_mv)
{
    return interp_percent(k_loaded_curve,
                          sizeof(k_loaded_curve) / sizeof(k_loaded_curve[0]),
                          pack_mv);
}

uint8_t battery_percent_from_unloaded_mv(uint32_t pack_mv)
{
    return interp_percent(k_unloaded_curve,
                          sizeof(k_unloaded_curve) / sizeof(k_unloaded_curve[0]),
                          pack_mv);
}

uint8_t battery_zcl_voltage_from_mv(uint32_t pack_mv)
{
    uint32_t dv = (pack_mv + 50) / 100; /* 100 mV units, rounded */
    if (dv > 0xFE) {                     /* 0xFF is "invalid" in ZCL */
        dv = 0xFE;
    }
    return (uint8_t)dv;
}

uint8_t battery_zcl_percentage_from_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return (uint8_t)(percent * 2); /* 0.5 % units, 0..200 */
}
