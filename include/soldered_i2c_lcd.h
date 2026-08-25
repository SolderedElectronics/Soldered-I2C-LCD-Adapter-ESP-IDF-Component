/**
 * @file soldered_i2c_lcd.h
 * @brief Public API for the soldered-i2c-lcd component
 * @author Soldered Electronics
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default 7-bit I2C address (A0/A1/A2 jumpers unpopulated) */
#define I2C_LCD_DEFAULT_ADDR 0x20

/** Character grid size of the attached HD44780 display */
#define I2C_LCD_COLS 16
#define I2C_LCD_ROWS 2

/** Number of CGRAM custom character slots (indices 0..7) */
#define I2C_LCD_CUSTOM_CHAR_COUNT 8

/**
 * @brief I2C LCD adapter device handle
 *
 * Owned by the caller (no heap allocation, no delete/deinit function). Zero
 * or stack-allocate it, then pass its address to i2c_lcd_init() before use.
 */
typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t display_control; // cached DISPLAYCONTROL flags (display/cursor/blink on-off)
    bool backlight_on;
} i2c_lcd_t;

/**
 * @brief Initialize the HD44780 16x2 display behind the TCA9534 I2C GPIO expander
 *
 * Configures the expander's 8 pins as outputs, then runs the HD44780 4-bit
 * power-on initialization sequence (function set, display on, entry mode,
 * clear). Backlight defaults to on. Contrast is set by the board's onboard
 * potentiometer, not by this driver.
 *
 * @param[out] lcd     Device handle to initialize
 * @param[in]  bus     I2C master bus handle the adapter is attached to
 * @param[in]  i2c_addr 7-bit I2C address (I2C_LCD_DEFAULT_ADDR unless A0/A1/A2 are populated)
 *
 * @return ESP_OK on success, or an error from the underlying I2C transactions
 */
esp_err_t i2c_lcd_init(i2c_lcd_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_addr);

/**
 * @brief Clear the display and return the cursor to (0, 0)
 *
 * @param[in] lcd Initialized device handle
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_clear(i2c_lcd_t *lcd);

/**
 * @brief Return the cursor to (0, 0) without clearing the display
 *
 * @param[in] lcd Initialized device handle
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_home(i2c_lcd_t *lcd);

/**
 * @brief Move the cursor to a given column/row
 *
 * @param[in] lcd Initialized device handle
 * @param[in] col Column, 0..I2C_LCD_COLS-1
 * @param[in] row Row, 0..I2C_LCD_ROWS-1
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if col/row is out of range
 */
esp_err_t i2c_lcd_set_cursor(i2c_lcd_t *lcd, uint8_t col, uint8_t row);

/**
 * @brief Write one character at the current cursor position
 *
 * @param[in] lcd Initialized device handle
 * @param[in] c   Character to write
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_print_char(i2c_lcd_t *lcd, char c);

/**
 * @brief Write a null-terminated string at the current cursor position
 *
 * Stops at the first error, if any. Does not wrap or clip at the end of a
 * row — writing past column I2C_LCD_COLS-1 follows the HD44780's own DDRAM
 * addressing (wraps into the display's off-screen DDRAM, not onto the next row).
 *
 * @param[in] lcd Initialized device handle
 * @param[in] str Null-terminated string to write
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_print(i2c_lcd_t *lcd, const char *str);

/**
 * @brief Turn the display on or off
 *
 * When off, the display is blanked but DDRAM/CGRAM content and cursor
 * position are retained.
 *
 * @param[in] lcd Initialized device handle
 * @param[in] on  true to turn the display on, false to turn it off
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_display(i2c_lcd_t *lcd, bool on);

/**
 * @brief Show or hide the underline cursor
 *
 * @param[in] lcd Initialized device handle
 * @param[in] on  true to show the cursor, false to hide it
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_cursor(i2c_lcd_t *lcd, bool on);

/**
 * @brief Enable or disable the blinking block cursor
 *
 * @param[in] lcd Initialized device handle
 * @param[in] on  true to enable blinking, false to disable it
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_blink(i2c_lcd_t *lcd, bool on);

/**
 * @brief Switch the backlight on or off
 *
 * @param[in] lcd Initialized device handle
 * @param[in] on  true to turn the backlight on, false to turn it off
 *
 * @return ESP_OK on success
 */
esp_err_t i2c_lcd_backlight(i2c_lcd_t *lcd, bool on);

/**
 * @brief Define a custom character in CGRAM
 *
 * Leaves the cursor at (0, 0) afterwards, since writing a custom character
 * moves the internal address pointer into CGRAM, not DDRAM. Use the printed
 * value 0..I2C_LCD_CUSTOM_CHAR_COUNT-1 with i2c_lcd_print_char() to display it.
 *
 * @param[in] lcd     Initialized device handle
 * @param[in] slot    Custom character slot, 0..I2C_LCD_CUSTOM_CHAR_COUNT-1
 * @param[in] pattern 8 bytes, one per row, bits 4:0 set the lit pixels (5x8 font)
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if slot is out of range
 */
esp_err_t i2c_lcd_create_char(i2c_lcd_t *lcd, uint8_t slot, const uint8_t pattern[8]);

#ifdef __cplusplus
}
#endif
