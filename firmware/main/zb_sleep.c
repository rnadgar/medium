/*
 * zb_sleep.c — light-sleep PM + poll helpers. See zb_sleep.h.
 */
#include "zb_sleep.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"

#include "esp_zigbee_core.h"

static const char *TAG = "zb_sleep";

esp_err_t zb_sleep_init(void)
{
#ifdef CONFIG_PM_ENABLE
    /* Sleepy-end-device example pattern: keep max==min==default CPU freq
     * and let automatic light sleep (tickless idle) do the saving. */
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = true,
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure: %s", esp_err_to_name(err));
        return err;
    }
#endif
    /* Let the ZBOSS scheduler light-sleep between MAC data polls. Default
     * sleep threshold (~20 ms) is fine for our timers. Must precede
     * esp_zb_init(). */
    esp_zb_sleep_enable(true);
    return ESP_OK;
}

void zb_sleep_set_long_poll_ms(uint32_t ms)
{
    /* VERIFY AT FIRST TARGET BUILD: esp_zb_zdo_pim_set_long_poll_interval()
     * is the esp-zigbee-lib wrapper of the ZBOSS PIM API (declared in
     * esp_zigbee_zdo_common.h in SDK 1.x). Call from the Zigbee task or
     * under esp_zb_lock. */
    esp_zb_zdo_pim_set_long_poll_interval(ms);
    ESP_LOGI(TAG, "long poll = %lu ms", (unsigned long)ms);
}

void zb_sleep_fast_poll_window(uint32_t ms)
{
    /* VERIFY AT FIRST TARGET BUILD: turbo-poll (fast poll) window API.
     * esp-zigbee-lib exposes esp_zb_zdo_pim_start_turbo_poll_continuous()
     * (poll rapidly for the given number of milliseconds, then revert to
     * the long poll interval). If the deployed SDK version only has
     * esp_zb_zdo_pim_start_turbo_poll_packets(n), substitute that here —
     * this wrapper is the single call site. */
    esp_zb_zdo_pim_start_turbo_poll_continuous(ms);
    ESP_LOGD(TAG, "fast poll window %lu ms", (unsigned long)ms);
}
