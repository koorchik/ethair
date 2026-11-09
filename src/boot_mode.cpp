#include "boot_mode.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logic/boot_guard.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

namespace {
const char* TAG = "boot";
RTC_NOINIT_ATTR BootGuardState s_guard;
bool s_safe = false;

ResetKind resetKind() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
        case ESP_RST_BROWNOUT:
            return ResetKind::PowerOn;
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return ResetKind::Crash;
        case ESP_RST_SW:
            return ResetKind::Software;
        default:
            return ResetKind::Other;
    }
}
}  // namespace

void bootModeReconnectSerialJtag() {
    // Route the internal PHY back to the serial/JTAG controller. These RTC
    // bits survive software resets, so after TinyUSB (or a crash while it ran)
    // the PHY would otherwise stay with USB-OTG.
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    // Let go of the pads and hold D+/D- low so the host sees a disconnect.
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    const gpio_num_t pins[] = {GPIO_NUM_19, GPIO_NUM_20};
    for (gpio_num_t p : pins) {
        gpio_reset_pin(p);
        gpio_set_direction(p, GPIO_MODE_OUTPUT_OD);
        gpio_set_level(p, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    for (gpio_num_t p : pins) gpio_reset_pin(p);
    // Give the pads back to serial/JTAG. Without this the port stays dead
    // (the Arduino core does the same in usb_switch_to_cdc_jtag()).
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
}

bool bootModeIsSafe() {
    s_safe = bootGuardOnBoot(s_guard, resetKind());
    ESP_LOGI(TAG, "reset reason %d, crashes %lu -> %s", (int)esp_reset_reason(),
             (unsigned long)s_guard.crashCount, s_safe ? "SAFE MODE" : "normal");
    if (s_safe) bootModeReconnectSerialJtag();
    return s_safe;
}

void bootModeMarkStable() {
    bootGuardMarkStable(s_guard);
}

void bootModeRestartSafe() {
    ESP_LOGW(TAG, "restarting into safe mode");
    bootGuardRequestSafe(s_guard);
    bootModeReconnectSerialJtag();
    esp_restart();
}

void bootModeRestartNormal() {
    ESP_LOGW(TAG, "restarting");
    bootModeReconnectSerialJtag();
    esp_restart();
}

void bootModeRestartDownload() {
    ESP_LOGW(TAG, "restarting into the ROM download mode");
    bootModeReconnectSerialJtag();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
