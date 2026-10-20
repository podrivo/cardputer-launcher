// Tiny demo app for Mini Launcher packing tests. Do not call mark_app_valid.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define M5GFX_BOARD lgfx::boards::board_M5CardputerADV
#include <M5GFX.h>

extern "C" void app_main(void)
{
    M5GFX display;
    display.init();
    display.setRotation(1);
    display.fillScreen(0x0000);
    display.setTextColor(0x07E0, 0x0000);
    display.setTextSize(2);
    display.setCursor(8, 40);
    display.print("Demo App");
    display.setTextSize(1);
    display.setTextColor(0xFFFF, 0x0000);
    display.setCursor(8, 80);
    display.print("Power off -> launcher");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
