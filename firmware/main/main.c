/*
 * main.c — smart register firmware entry point.
 *
 * Init order (docs/architecture.md): NVS -> board/stepper -> motion ->
 * pressure (bus only; sampling starts deferred once the Zigbee stack is up)
 * -> power management -> Zigbee task. Plus two local chores: the BOOT
 * button long-press (factory reset / re-pair) and the daily unloaded
 * battery housekeeping sample.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_zigbee_core.h"

#include "battery.h"
#include "board.h"
#include "nvs_state.h"
#include "pressure.h"
#include "stepper.h"
#include "zb_device.h"
#include "zb_sleep.h"

static const char *TAG = "main";

#define FACTORY_RESET_HOLD_MS  5000
#define BATTERY_PERIOD_MS      (24 * 60 * 60 * 1000) /* daily */
#define BATTERY_FIRST_DELAY_MS 30000

/* ---- BOOT button: long-press = factory reset / re-pair ------------------ */

static esp_timer_handle_t s_btn_timer;

static void btn_hold_check_cb(void *arg)
{
    (void)arg;
    if (gpio_get_level(PIN_BOOT_BTN) == 0) {
        ESP_LOGW(TAG, "BOOT held %d ms -> Zigbee factory reset",
                 FACTORY_RESET_HOLD_MS);
        /* Erases zb_storage and reboots into pairing (steering). */
        esp_zb_factory_reset();
    }
}

static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    /* Falling edge: start (or restart) the hold timer from ISR. */
    esp_timer_stop(s_btn_timer);
    esp_timer_start_once(s_btn_timer, (uint64_t)FACTORY_RESET_HOLD_MS * 1000);
}

static void button_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* strap pin, active low */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    const esp_timer_create_args_t targs = {
        .callback = btn_hold_check_cb,
        .name = "btn_hold",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_btn_timer));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BOOT_BTN, btn_isr, NULL));
}

/* ---- Daily unloaded battery sample --------------------------------------
 * The honest state-of-charge number comes from the loaded sample taken
 * during moves (stepper.c -> on_battery_loaded); this daily unloaded
 * reading is a low-rate backstop for registers that rarely move. */
static void battery_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BATTERY_FIRST_DELAY_MS));
    for (;;) {
        uint32_t mv = battery_hw_read_pack_mv();
        if (mv > 0) {
            uint8_t pct = battery_percent_from_unloaded_mv(mv);
            ESP_LOGI(TAG, "battery (unloaded): %lu mV ~ %u %%",
                     (unsigned long)mv, pct);
            zb_device_update_battery(mv, pct);
        }
        vTaskDelay(pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}

/* ---- Entry point --------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "smart register starting");

    /* 1. NVS first — everything below restores state from it. */
    ESP_ERROR_CHECK(nvs_state_init());

    /* 2. Board + stepper hardware (GPIO, GPTimer; rails stay off). */
    ESP_ERROR_CHECK(stepper_init());
    ESP_ERROR_CHECK(battery_hw_init() == 0 ? ESP_OK : ESP_FAIL);
    button_init();

    /* 3. Motion controller: restores position/range, forces a re-home if a
     *    move was interrupted (move_pending flag), wires reporting callbacks
     *    into zb_device. */
    ESP_ERROR_CHECK(motion_start(
        (const motion_callbacks_t *)zb_device_motion_callbacks()));

    /* 4. Pressure: fail-safe config from NVS + I2C bus. Sampling itself
     *    starts via the deferred driver init in zb_device.c once the stack
     *    is running. */
    ESP_ERROR_CHECK(pressure_init());

    /* 5. Zigbee platform + power management. zb_sleep_init() must precede
     *    esp_zb_init() (inside the zigbee task) — the stack latches the
     *    sleep-enable flag at init. */
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));
    ESP_ERROR_CHECK(zb_sleep_init());

    /* 6. Zigbee device task (clusters, commissioning, main loop). */
    ESP_ERROR_CHECK(zb_device_start());

    /* 7. Housekeeping. */
    xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "init complete (open=%u%%, fault=%u)",
             motion_current_open_pct(), motion_fault_code());
}
