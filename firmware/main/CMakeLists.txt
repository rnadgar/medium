idf_component_register(
    SRCS
        "main.c"
        "zb_device.c"
        "zb_sleep.c"
        "stepper.c"
        "damper.c"      # pure logic — also compiled by test/host
        "failsafe.c"    # pure logic — also compiled by test/host
        "battery.c"     # pure logic — also compiled by test/host
        "battery_hw.c"
        "pressure.c"
        "nvs_state.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        esp_timer
        esp_pm
        esp_adc
        nvs_flash
)
