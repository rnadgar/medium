/* nvs_state.c — persisted device state. See nvs_state.h. */
#include "nvs_state.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "failsafe.h" /* pure header: defaults for fail-safe attrs */

static const char *TAG = "nvs_state";
static const char *NS = "sregister";

static nvs_handle_t s_handle;
static bool s_open;

esp_err_t nvs_state_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(NS, NVS_READWRITE, &s_handle);
    s_open = (err == ESP_OK);
    return err;
}

static void commit_or_warn(esp_err_t err, const char *key)
{
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s failed: %s", key, esp_err_to_name(err));
    }
}

uint8_t nvs_state_get_open_pct(uint8_t fallback)
{
    uint8_t v = fallback;
    if (s_open) {
        nvs_get_u8(s_handle, "open_pct", &v);
    }
    return v;
}

void nvs_state_set_open_pct(uint8_t open_pct)
{
    if (s_open) {
        commit_or_warn(nvs_set_u8(s_handle, "open_pct", open_pct), "open_pct");
    }
}

int32_t nvs_state_get_range_steps(int32_t fallback)
{
    int32_t v = fallback;
    if (s_open) {
        nvs_get_i32(s_handle, "range", &v);
    }
    return v;
}

void nvs_state_set_range_steps(int32_t range_steps)
{
    if (s_open) {
        commit_or_warn(nvs_set_i32(s_handle, "range", range_steps), "range");
    }
}

bool nvs_state_get_move_pending(void)
{
    uint8_t v = 0;
    if (s_open) {
        nvs_get_u8(s_handle, "move_pend", &v);
    }
    return v != 0;
}

void nvs_state_set_move_pending(bool pending)
{
    if (s_open) {
        commit_or_warn(nvs_set_u8(s_handle, "move_pend", pending ? 1 : 0),
                       "move_pend");
    }
}

void nvs_state_get_failsafe(uint16_t *threshold_pa, uint16_t *hysteresis_pa,
                            bool *auto_clear)
{
    failsafe_config_t def = failsafe_default_config();
    uint16_t thr = def.threshold_pa;
    uint16_t hyst = def.clear_hysteresis_pa;
    uint8_t ac = def.auto_clear_enable ? 1 : 0;
    if (s_open) {
        nvs_get_u16(s_handle, "fs_thresh", &thr);
        nvs_get_u16(s_handle, "fs_hyst", &hyst);
        nvs_get_u8(s_handle, "fs_autoclr", &ac);
    }
    if (threshold_pa) {
        *threshold_pa = thr;
    }
    if (hysteresis_pa) {
        *hysteresis_pa = hyst;
    }
    if (auto_clear) {
        *auto_clear = (ac != 0);
    }
}

void nvs_state_set_failsafe(uint16_t threshold_pa, uint16_t hysteresis_pa,
                            bool auto_clear)
{
    if (!s_open) {
        return;
    }
    esp_err_t err = nvs_set_u16(s_handle, "fs_thresh", threshold_pa);
    if (err == ESP_OK) {
        err = nvs_set_u16(s_handle, "fs_hyst", hysteresis_pa);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(s_handle, "fs_autoclr", auto_clear ? 1 : 0);
    }
    commit_or_warn(err, "fs_*");
}
