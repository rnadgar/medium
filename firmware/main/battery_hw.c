/*
 * battery_hw.c — target-only ADC shim for battery.c (which is pure logic).
 *
 * VBAT_SENSE (GPIO0 / ADC1_CH0) sits behind a 1 M / 330 k divider that is
 * only connected while VBAT_SENSE_EN is high (NFET -> P-FET gate), per the
 * docs/power-budget.md "no continuous pull-ups anywhere" rule.
 */
#include "battery.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "board.h"

static const char *TAG = "battery_hw";

#define VBAT_ADC_UNIT       ADC_UNIT_1
#define VBAT_ADC_ATTEN      ADC_ATTEN_DB_12 /* full pin range ~0-3.3 V */
#define VBAT_SAMPLES        8
#define DIVIDER_SETTLE_MS   2   /* RC of 1 M||330 k into the pin capacitance */

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_ready;

int battery_hw_init(void)
{
    gpio_config_t en = {
        .pin_bit_mask = 1ULL << PIN_VBAT_SENSE_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&en) != ESP_OK) {
        return -1;
    }
    gpio_set_level(PIN_VBAT_SENSE_EN, 0);

    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = VBAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) {
        return -1;
    }
    adc_oneshot_chan_cfg_t ccfg = {
        .atten = VBAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc, VBAT_ADC_CHANNEL, &ccfg) != ESP_OK) {
        return -1;
    }

    /* ESP32-C6 supports curve-fitting calibration. */
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = VBAT_ADC_UNIT,
        .chan = VBAT_ADC_CHANNEL,
        .atten = VBAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration available, using raw estimate");
        s_cali = NULL;
    }
    s_ready = true;
    return 0;
}

uint32_t battery_hw_read_pack_mv(void)
{
    if (!s_ready) {
        return 0;
    }

    gpio_set_level(PIN_VBAT_SENSE_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(DIVIDER_SETTLE_MS));

    uint32_t sum_mv = 0;
    int good = 0;
    for (int i = 0; i < VBAT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, VBAT_ADC_CHANNEL, &raw) != ESP_OK) {
            continue;
        }
        int mv;
        if (s_cali != NULL &&
            adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            sum_mv += (uint32_t)mv;
        } else {
            /* Uncalibrated fallback: 12-bit full scale ~= 3300 mV at 12 dB. */
            sum_mv += (uint32_t)raw * 3300 / 4095;
        }
        good++;
    }

    gpio_set_level(PIN_VBAT_SENSE_EN, 0);

    if (good == 0) {
        return 0;
    }
    uint32_t pin_mv = sum_mv / (uint32_t)good;
    return pin_mv * VBAT_DIV_NUM / VBAT_DIV_DEN;
}
