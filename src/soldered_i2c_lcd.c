/**
 * @file soldered_i2c_lcd.c
 * @brief Implementation for the soldered-i2c-lcd component
 * @author Soldered Electronics
 */

#include "soldered_i2c_lcd.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// -1 disables the driver's timeout instead of extending it, which lets
// i2c_master_transmit()/receive() spin forever on a busy bus and starve the
// idle task until the watchdog fires. Keep this bounded.
#define I2C_LCD_I2C_TIMEOUT_MS 1000

// TCA9534 registers (datasheet Table 6)
#define TCA9534_REG_OUTPUT 0x01
#define TCA9534_REG_CONFIG 0x03 // bit=1 -> input, bit=0 -> output

// TCA9534 P0..P7 wiring to the HD44780, per the easyC LCD driver schematic.
#define I2C_LCD_BIT_RS 0x01 // P0
#define I2C_LCD_BIT_RW 0x02 // P1 (wired but unused, always write)
#define I2C_LCD_BIT_EN 0x04 // P2
#define I2C_LCD_BIT_BL 0x08 // P3, active-high (NMOS pulls backlight cathode low)
#define I2C_LCD_DATA_SHIFT 4 // P4..P7 = D4..D7

// HD44780 command set (datasheet Table 6)
#define HD44780_CMD_CLEAR_DISPLAY   0x01
#define HD44780_CMD_RETURN_HOME     0x02
#define HD44780_CMD_ENTRY_MODE_SET  0x04
#define HD44780_CMD_DISPLAY_CONTROL 0x08
#define HD44780_CMD_FUNCTION_SET    0x20
#define HD44780_CMD_SET_CGRAM_ADDR  0x40
#define HD44780_CMD_SET_DDRAM_ADDR  0x80

#define HD44780_ENTRY_LEFT       0x02
#define HD44780_ENTRY_SHIFT_NONE 0x00

#define HD44780_DISPLAY_ON  0x04
#define HD44780_CURSOR_ON   0x02
#define HD44780_BLINK_ON    0x01

#define HD44780_FUNCTION_4BIT 0x00
#define HD44780_FUNCTION_2LINE 0x08
#define HD44780_FUNCTION_5X8DOTS 0x00

// Row 1 starts at DDRAM address 0x40 on a 2-line HD44780 display.
static const uint8_t HD44780_ROW_OFFSETS[I2C_LCD_ROWS] = { 0x00, 0x40 };

static esp_err_t i2c_lcd_write_port(i2c_lcd_t *lcd, uint8_t value)
{
    uint8_t buf[2] = { TCA9534_REG_OUTPUT, value };
    return i2c_master_transmit(lcd->dev, buf, sizeof(buf), I2C_LCD_I2C_TIMEOUT_MS);
}

// Pulses E high then low with the given RS level and data nibble in bits 7:4.
// Settle delay is the caller's job (i2c_lcd_command()/i2c_lcd_data()), since
// it depends on which HD44780 command was just sent.
static esp_err_t i2c_lcd_write_nibble(i2c_lcd_t *lcd, uint8_t nibble, bool rs)
{
    uint8_t base = (uint8_t)((nibble << I2C_LCD_DATA_SHIFT) | (rs ? I2C_LCD_BIT_RS : 0) |
                             (lcd->backlight_on ? I2C_LCD_BIT_BL : 0));

    esp_err_t err = i2c_lcd_write_port(lcd, (uint8_t)(base | I2C_LCD_BIT_EN));

    if (err != ESP_OK) {
        return err;
    }

    return i2c_lcd_write_port(lcd, base);
}

static esp_err_t i2c_lcd_send_byte(i2c_lcd_t *lcd, uint8_t value, bool rs)
{
    esp_err_t err = i2c_lcd_write_nibble(lcd, (uint8_t)(value >> 4), rs);

    if (err != ESP_OK) {
        return err;
    }

    return i2c_lcd_write_nibble(lcd, (uint8_t)(value & 0x0F), rs);
}

// Datasheet settle time is 37us for most commands; round up to a full tick.
static esp_err_t i2c_lcd_command(i2c_lcd_t *lcd, uint8_t cmd)
{
    esp_err_t err = i2c_lcd_send_byte(lcd, cmd, false);

    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

static esp_err_t i2c_lcd_data(i2c_lcd_t *lcd, uint8_t value)
{
    esp_err_t err = i2c_lcd_send_byte(lcd, value, true);

    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

esp_err_t i2c_lcd_init(i2c_lcd_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_addr)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &lcd->dev);

    if (err != ESP_OK) {
        return err;
    }

    lcd->backlight_on = true;
    lcd->display_control = HD44780_DISPLAY_ON;

    uint8_t config_buf[2] = { TCA9534_REG_CONFIG, 0x00 }; // all 8 pins as outputs
    err = i2c_master_transmit(lcd->dev, config_buf, sizeof(config_buf), I2C_LCD_I2C_TIMEOUT_MS);

    if (err != ESP_OK) {
        return err;
    }

    // HD44780 power-on init (datasheet Figure 24, 4-bit interface):
    // wait >40ms after Vcc rises, then force the controller into a known
    // state with three 8-bit-mode nibbles before switching to 4-bit mode.
    vTaskDelay(pdMS_TO_TICKS(50));

    err = i2c_lcd_write_nibble(lcd, 0x03, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    err = i2c_lcd_write_nibble(lcd, 0x03, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    err = i2c_lcd_write_nibble(lcd, 0x03, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    err = i2c_lcd_write_nibble(lcd, 0x02, false); // switch to 4-bit mode
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    err = i2c_lcd_command(lcd, HD44780_CMD_FUNCTION_SET | HD44780_FUNCTION_4BIT | HD44780_FUNCTION_2LINE |
                          HD44780_FUNCTION_5X8DOTS);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_lcd_command(lcd, HD44780_CMD_DISPLAY_CONTROL | lcd->display_control);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_lcd_clear(lcd);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_lcd_command(lcd, HD44780_CMD_ENTRY_MODE_SET | HD44780_ENTRY_LEFT | HD44780_ENTRY_SHIFT_NONE);
}

esp_err_t i2c_lcd_clear(i2c_lcd_t *lcd)
{
    esp_err_t err = i2c_lcd_command(lcd, HD44780_CMD_CLEAR_DISPLAY);

    if (err != ESP_OK) {
        return err;
    }

    // Clear/home need the full ~1.52ms instruction time, not the usual 37us.
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

esp_err_t i2c_lcd_home(i2c_lcd_t *lcd)
{
    esp_err_t err = i2c_lcd_command(lcd, HD44780_CMD_RETURN_HOME);

    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

esp_err_t i2c_lcd_set_cursor(i2c_lcd_t *lcd, uint8_t col, uint8_t row)
{
    if (col >= I2C_LCD_COLS || row >= I2C_LCD_ROWS) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_lcd_command(lcd, HD44780_CMD_SET_DDRAM_ADDR | (uint8_t)(HD44780_ROW_OFFSETS[row] + col));
}

esp_err_t i2c_lcd_print_char(i2c_lcd_t *lcd, char c)
{
    return i2c_lcd_data(lcd, (uint8_t)c);
}

esp_err_t i2c_lcd_print(i2c_lcd_t *lcd, const char *str)
{
    while (*str != '\0') {
        esp_err_t err = i2c_lcd_data(lcd, (uint8_t) * str);

        if (err != ESP_OK) {
            return err;
        }

        str++;
    }

    return ESP_OK;
}

esp_err_t i2c_lcd_display(i2c_lcd_t *lcd, bool on)
{
    if (on) {
        lcd->display_control |= HD44780_DISPLAY_ON;
    } else {
        lcd->display_control &= (uint8_t) ~HD44780_DISPLAY_ON;
    }

    return i2c_lcd_command(lcd, HD44780_CMD_DISPLAY_CONTROL | lcd->display_control);
}

esp_err_t i2c_lcd_cursor(i2c_lcd_t *lcd, bool on)
{
    if (on) {
        lcd->display_control |= HD44780_CURSOR_ON;
    } else {
        lcd->display_control &= (uint8_t) ~HD44780_CURSOR_ON;
    }

    return i2c_lcd_command(lcd, HD44780_CMD_DISPLAY_CONTROL | lcd->display_control);
}

esp_err_t i2c_lcd_blink(i2c_lcd_t *lcd, bool on)
{
    if (on) {
        lcd->display_control |= HD44780_BLINK_ON;
    } else {
        lcd->display_control &= (uint8_t) ~HD44780_BLINK_ON;
    }

    return i2c_lcd_command(lcd, HD44780_CMD_DISPLAY_CONTROL | lcd->display_control);
}

esp_err_t i2c_lcd_backlight(i2c_lcd_t *lcd, bool on)
{
    lcd->backlight_on = on;

    // No RS/E transition happens here, so the backlight bit must be pushed
    // with an otherwise-idle nibble write to actually reach the expander.
    return i2c_lcd_write_port(lcd, on ? I2C_LCD_BIT_BL : 0x00);
}

esp_err_t i2c_lcd_create_char(i2c_lcd_t *lcd, uint8_t slot, const uint8_t pattern[8])
{
    if (slot >= I2C_LCD_CUSTOM_CHAR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_lcd_command(lcd, HD44780_CMD_SET_CGRAM_ADDR | (uint8_t)(slot << 3));

    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 8; i++) {
        err = i2c_lcd_data(lcd, pattern[i]);

        if (err != ESP_OK) {
            return err;
        }
    }

    return i2c_lcd_set_cursor(lcd, 0, 0);
}
