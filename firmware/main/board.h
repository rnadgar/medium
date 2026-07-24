/*
 * board.h — pin map for the smart register.
 *
 * SINGLE SOURCE OF TRUTH is the pin table in docs/architecture.md; this
 * header mirrors it exactly. The board variant is selected with
 * `idf.py menuconfig` -> "Smart Register Board" (Kconfig.projbuild):
 *   CONFIG_BOARD_DEVKITC1 — ESP32-C6-DevKitC-1 bring-up jig (default)
 *   CONFIG_BOARD_REVA     — Rev A register PCB
 *
 * The two variants differ only in the STATUS_LED pin (Rev A: GPIO20
 * discrete LED; DevKitC-1: GPIO8 onboard LED*).
 *
 * NOTE: GPIO10/11 are NOT bonded out on the ESP32-C6-MINI-1 module — never
 * assign them. Free spares: GPIO14, GPIO15.
 *
 * *The DevKitC-1 "LED" on GPIO8 is a WS2812 addressable part; driven as a
 * plain GPIO it will not light. Good enough for logic-analyzer bring-up;
 * attach a discrete LED to GPIO8 if a visible identify blink is needed.
 */
#ifndef SMART_REGISTER_BOARD_H
#define SMART_REGISTER_BOARD_H

/* --- Battery measurement ------------------------------------------------- */
#define PIN_VBAT_SENSE      0   /* ADC1_CH0, 1 M / 330 k divider */
#define PIN_VBAT_SENSE_EN   1   /* high = divider connected (NFET -> P-FET) */
#define VBAT_ADC_CHANNEL    0   /* ADC1 channel for GPIO0 on ESP32-C6 */

/* Divider ratio: Vpin = Vbat * 330k / (1M + 330k). */
#define VBAT_DIV_NUM        1330
#define VBAT_DIV_DEN        330

/* --- Stepper driver (DRV8833, 28BYJ-48 bipolar mod) ---------------------- */
#define PIN_MOTOR_AIN1      2
#define PIN_MOTOR_AIN2      3
#define PIN_MOTOR_BIN1      4
#define PIN_MOTOR_BIN2      5

/* --- Pressure sensor I2C (on switched 3V3_SENS rail) --------------------- */
#define PIN_I2C_SDA         6
#define PIN_I2C_SCL         7

/* --- Buttons ------------------------------------------------------------- */
#define PIN_BOOT_BTN        9   /* boot strap; long-press = factory reset / pair */

/* --- Debug UART header --------------------------------------------------- */
#define PIN_UART_TX         16
#define PIN_UART_RX         17

/* --- Limit switches (active low, only valid while LIMIT_SENSE_EN high) --- */
#define PIN_LIMIT_OPEN      18  /* closes to GND at full-open travel */
#define PIN_LIMIT_CLOSED    19  /* closes to GND at full-closed travel */

/* --- Power gating (docs/power-budget.md design rules) ---------------------
 * One GPIO gates BOTH switched rails: VMOT (TPS22810 from VBAT: DRV8833)
 * and 3V3_SENS (SiP32431 from 3.3 V: pressure sensor + I2C pull-ups). */
#define PIN_MOTOR_PWR_EN    21  /* high = motor + sensor rails powered */
#define PIN_LIMIT_SENSE_EN  22  /* sources limit-switch pull-ups during moves only */

/* --- Remote-sensor header spare ------------------------------------------ */
#define PIN_SENS_IRQ        23  /* reserved (input) */

/* --- Status LED ---------------------------------------------------------- */
#if defined(CONFIG_BOARD_REVA)
#define PIN_STATUS_LED      20
#elif defined(CONFIG_BOARD_DEVKITC1)
#define PIN_STATUS_LED      8   /* onboard (see note above) */
#else
/* Host builds never include this header; on target one board must be chosen. */
#define PIN_STATUS_LED      8
#endif

#endif /* SMART_REGISTER_BOARD_H */
