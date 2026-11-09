#include "bme688.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_log.h>

#define TAG "BME688"

#define BME688_RESET_ADDR           0xE0
#define BME688_CHIP_ID_ADDR         0xD0
#define BME688_CHIP_ID              0x61

#define BME688_CALIB_1_ADDR         0x8A
#define BME688_CALIB_1_LEN          23
#define BME688_CALIB_2_ADDR         0xE1
#define BME688_CALIB_2_LEN          14

#define BME688_CTRL_GAS_0_ADDR      0x70
#define BME688_CTRL_GAS_1_ADDR      0x71
#define BME688_CTRL_HUM_ADDR        0x72
#define BME688_CTRL_MEAS_ADDR       0x74
#define BME688_CONFIG_ADDR          0x75
#define BME688_MEAS_STATUS_0_ADDR   0x1D
#define BME688_DATA_LEN             10
#define BME688_STATUS_NEW_DATA      (1 << 7)
#define BME688_STATUS_MEASURING     (1 << 5)

#define I2C_TIMEOUT_MS              100

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static bool sensor_initialized = false;

// Variables de calibración de fábrica
static uint16_t par_t1; static int16_t par_t2; static int8_t par_t3;
static uint16_t par_p1; static int16_t par_p2; static int8_t par_p3;
static int16_t par_p4; static int16_t par_p5; static int8_t par_p6;
static int8_t par_p7; static int16_t par_p8; static int16_t par_p9;
static uint8_t par_p10;
static uint16_t par_h1; static uint16_t par_h2; static int8_t par_h3;
static int8_t par_h4; static int8_t par_h5; static uint8_t par_h6;
static int8_t par_h7;

static double last_t_fine = 128000.0;
static double last_temp_comp = 25.0;

esp_err_t bme688_init(void) {
    esp_err_t err;

    // crear bus I2C em GPIO 15 (SDA) y GPIO 16 (SCL) solo una vez
    if (bus_handle == NULL) {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = BME688_SDA_PIN,
            .scl_io_num = BME688_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bus_config, &bus_handle);
        if (err != ESP_OK) return err;
    }

    // detectar automaticamente si el BME688 responde en 0x76 o 0x77
    if (dev_handle == NULL) {

        uint8_t found_SLV_addr = 0;
        if (i2c_master_probe(bus_handle, 0x76, I2C_TIMEOUT_MS) == ESP_OK) {
            found_SLV_addr = 0x76;
        } else if (i2c_master_probe(bus_handle, 0x77, I2C_TIMEOUT_MS) == ESP_OK) {
            found_SLV_addr = 0x77;
        } else { 
            return ESP_ERR_NOT_FOUND;
        }

        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = found_SLV_addr,
            .scl_speed_hz = 400000,
        };
        err = i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
        if (err != ESP_OK) return err;
    }

    // hacemos un soft reset (0xB6 en 0xE0) y espera de 10 ms
    uint8_t buf[2] = {BME688_RESET_ADDR, 0xB6};
    err = i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    // verificamos que el chip ID sea correcto (0x61 en 0xD0)
    buf[0] = BME688_CHIP_ID_ADDR;
    uint8_t chip_id = 0;
    err = i2c_master_transmit_receive(dev_handle, buf, 1, &chip_id, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK || chip_id != BME688_CHIP_ID) return ESP_ERR_INVALID_RESPONSE;

    // leemos los registros de calibración de fábrica
    uint16_t calib_1_w[(BME688_CALIB_1_LEN + 1) / 2];
    uint8_t *calib_1 = (uint8_t *)calib_1_w;
    buf[0] = BME688_CALIB_1_ADDR;
    err = i2c_master_transmit_receive(dev_handle, buf, 1, calib_1, BME688_CALIB_1_LEN, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    uint16_t calib_2_w[BME688_CALIB_2_LEN / 2];
    uint8_t *calib_2 = (uint8_t *)calib_2_w;
    buf[0] = BME688_CALIB_2_ADDR;
    err = i2c_master_transmit_receive(dev_handle, buf, 1, calib_2, BME688_CALIB_2_LEN, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    par_t1 = calib_2_w[4]; par_t2 = calib_1_w[0]; par_t3 = calib_1[2];
    par_p1 = calib_1_w[2]; par_p2 = calib_1_w[3]; par_p3 = calib_1[8];
    par_p4 = calib_1_w[5]; par_p5 = calib_1_w[6]; par_p6 = calib_1[15];
    par_p7 = calib_1[14]; par_p8 = calib_1_w[9]; par_p9 = calib_1_w[10];
    par_p10 = calib_1[22];
    par_h1 = (calib_2[2] << 4) | (calib_2[1] & 0x0F);
    par_h2 = (calib_2[0] << 4) | (calib_2[1] >> 4);
    par_h3 = calib_2[3]; par_h4 = calib_2[4]; par_h5 = calib_2[5];
    par_h6 = calib_2[6]; par_h7 = calib_2[7];

    // desactivamos el filtro IIR y el control de gas
    buf[0] = BME688_CONFIG_ADDR; buf[1] = 0x00;
    i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS);

    buf[0] = BME688_CTRL_GAS_0_ADDR; buf[1] = 0x08;
    i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS);

    buf[0] = BME688_CTRL_GAS_1_ADDR; buf[1] = 0x00;
    i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS);

    sensor_initialized = true;
    return ESP_OK;
}

esp_err_t bme688_read(bool req_t, bool req_h, bool req_p, bme688_data_t *out) {
    if (!sensor_initialized) {
        esp_err_t err = bme688_init();
        if (err != ESP_OK) return err;
    }

    uint8_t buf[2];
    uint8_t osrs_h = req_h ? 0x01 : 0x00;
    uint8_t osrs_p = req_p ? 0x01 : 0x00;
    uint8_t osrs_t = req_t ? 0x01 : 0x00;

    buf[0] = BME688_CTRL_HUM_ADDR; buf[1] = osrs_h;
    if (i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS) != ESP_OK) {
        sensor_initialized = false;
        return ESP_ERR_TIMEOUT;
    }

    buf[0] = BME688_CTRL_MEAS_ADDR; buf[1] = (osrs_t << 5) | (osrs_p << 2) | 0x01;
    if (i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS) != ESP_OK) {
        sensor_initialized = false;
        return ESP_ERR_TIMEOUT;
    }

    uint8_t data[BME688_DATA_LEN];
    bool ready = false;
    for (uint32_t tries = 0; tries < 20; tries++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        buf[0] = BME688_MEAS_STATUS_0_ADDR;
        if (i2c_master_transmit_receive(dev_handle, buf, 1, data, BME688_DATA_LEN, I2C_TIMEOUT_MS) != ESP_OK) {
            sensor_initialized = false;
            return ESP_ERR_TIMEOUT;
        }
        if ((data[0] & BME688_STATUS_NEW_DATA) && !(data[0] & BME688_STATUS_MEASURING)) {
            ready = true;
            break;
        }
    }

    if (!ready) return ESP_ERR_TIMEOUT;

    if (osrs_t) {
        uint32_t temp_adc = ((uint32_t)data[5] << 12) | ((uint32_t)data[6] << 4) | (data[7] >> 4);
        double var1 = ((double)temp_adc / 16384.0 - (double)par_t1 / 1024.0) * (double)par_t2;
        double d = (double)temp_adc / 131072.0 - (double)par_t1 / 8192.0;
        double var2 = d * d * ((double)par_t3 * 16.0);
        last_t_fine = var1 + var2;
        last_temp_comp = last_t_fine / 5120.0;
    }
    out->temperature_c = (float)last_temp_comp;

    if (req_p) {
        uint32_t press_adc = ((uint32_t)data[2] << 12) | ((uint32_t)data[3] << 4) | (data[4] >> 4);
        double var1 = (last_t_fine / 2.0) - 64000.0;
        double var2 = var1 * var1 * ((double)par_p6 / 131072.0);
        var2 = var2 + (var1 * (double)par_p5 * 2.0);
        var2 = (var2 / 4.0) + ((double)par_p4 * 65536.0);
        var1 = ((((double)par_p3 * var1 * var1) / 16384.0) + ((double)par_p2 * var1)) / 524288.0;
        var1 = (1.0 + (var1 / 32768.0)) * (double)par_p1;
        double press_comp = 1048576.0 - (double)press_adc;
        if (var1 != 0.0) {
            press_comp = ((press_comp - (var2 / 4096.0)) * 6250.0) / var1;
            var1 = ((double)par_p9 * press_comp * press_comp) / 2147483648.0;
            var2 = press_comp * ((double)par_p8 / 32768.0);
            double var3 = (press_comp / 256.0) * (press_comp / 256.0) * (press_comp / 256.0) * ((double)par_p10 / 131072.0);
            press_comp = press_comp + (var1 + var2 + var3 + ((double)par_p7 * 128.0)) / 16.0;
        } else {
            press_comp = 0.0;
        }
        out->pressure_hpa = (float)(press_comp / 100.0);
    } else {
        out->pressure_hpa = 0.0f;
    }

    if (req_h) {
        uint16_t hum_adc = (uint16_t)((data[8] << 8) | data[9]);
        double var1 = (double)hum_adc - (((double)par_h1 * 16.0) + (((double)par_h3 / 2.0) * last_temp_comp));
        double var2 = var1 * (((double)par_h2 / 262144.0) * (1.0 + (((double)par_h4 / 16384.0) * last_temp_comp) + (((double)par_h5 / 1048576.0) * last_temp_comp * last_temp_comp)));
        double var3 = (double)par_h6 / 16384.0;
        double var4 = (double)par_h7 / 2097152.0;
        double hum_comp = var2 + ((var3 + (var4 * last_temp_comp)) * var2 * var2);
        if (hum_comp > 100.0) hum_comp = 100.0;
        else if (hum_comp < 0.0) hum_comp = 0.0;
        out->humidity_rh = (float)hum_comp;
    } else {
        out->humidity_rh = 0.0f;
    }

    return ESP_OK;
}