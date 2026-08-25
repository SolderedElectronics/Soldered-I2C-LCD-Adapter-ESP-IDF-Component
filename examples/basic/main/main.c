/**
 * @file main.c
 * @brief Basic usage example for the soldered-i2c-lcd component
 * @author Soldered Electronics
 */

#include <stdio.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soldered_i2c_lcd.h"

// Adjust to whatever GPIOs the adapter is wired to on your board.
#define I2C_SDA_GPIO 8
#define I2C_SCL_GPIO 9

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_lcd_t lcd;
    esp_err_t err = i2c_lcd_init(&lcd, bus, I2C_LCD_DEFAULT_ADDR);

    if (err != ESP_OK) {
        printf("i2c_lcd_init failed: %s\n", esp_err_to_name(err));
        return;
    }

    i2c_lcd_print(&lcd, "Hello, Soldered!");
    i2c_lcd_set_cursor(&lcd, 0, 1);
    i2c_lcd_print(&lcd, "I2C LCD adapter");

    bool backlight_on = true;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        backlight_on = !backlight_on;
        i2c_lcd_backlight(&lcd, backlight_on);
    }
}
