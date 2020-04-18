/*

MIT License

Copyright (c) 2020 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

SPDX-License-Identifier: MIT

*/

#include <esp_log.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <string.h>

#include "sdkconfig.h"

static const char *TAG = "sdcard";

esp_err_t sdcard_init()
{
    ESP_LOGI(TAG, "Initializing SD card using SPI peripheral");

    esp_err_t status;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = VSPI_HOST;
    //host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    //sdspi_host_set_card_clk(VSPI_HOST, 40);

    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_SDCARD_PIN_MISO,
        .mosi_io_num = CONFIG_SDCARD_PIN_MOSI,
        .sclk_io_num = CONFIG_SDCARD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Max transfer size in bytes */
        .max_transfer_sz = 1024
    };
    /* TODO: make DMA channel configurable */
    status = spi_bus_initialize(VSPI_HOST, &buscfg, 2);

    if (status != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(status);
        return status;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SDCARD_PIN_CS;
    slot_config.host_id = VSPI_HOST;

    status = esp_vfs_fat_sdspi_mount(
        "/sdcard", &host, &slot_config, &mount_config, &card
    );

    if (status != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(status);
        return status;
    }

    /* Card has been initialized, print its properties. */
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}
