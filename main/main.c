/*

MIT No Attribution

Copyright (c) 2020 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

SPDX-License-Identifier: MIT-0

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_task_wdt.h>

#include <unistd.h>

#include <bitmap.h>
#include <rgb565.h>
#include <copepod.h>
#include <copepod_hal.h>
#include <font8x8.h>
#include <fps.h>
#include <bps.h>
#include <sdcard.h>

#include "tjpgd.h"
#include "sdkconfig.h"

static const char *TAG = "main";

static SemaphoreHandle_t mutex;
static float sd_fps;
static float sd_bps;
static bitmap_t *bb;

/*
 * Flush backbuffer to display. This is capped to 30 fps.
 * SD card reading is only 12.5 fps.
 */
void flush_task(void *params)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t frequency = 1000 / 30 / portTICK_RATE_MS;

    while (1) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        pod_flush();
        xSemaphoreGive(mutex);
        //fb_fps = fps();
        vTaskDelayUntil(&last, frequency);
    }


    vTaskDelete(NULL);
}

static uint16_t tjpgd_data_reader(JDEC *decoder, uint8_t *buffer, uint16_t size)
{
    FILE *fp = (FILE *)decoder->device;
    uint16_t bytes_read, bytes_skip;
    const uint16_t EOI = 0xffd9;
    uint8_t *eoi_ptr;

    if (buffer) {
        /* Read bytes from input stream. */
        //bytes_read = (uint16_t)fread(buffer, 1, size, fp);
        bytes_read = read(fileno(fp), buffer, size);

        sd_bps = bps(bytes_read);

        /* Search for EOI. */
        eoi_ptr = memmem(buffer, size, &EOI, 2);
        int16_t offset = eoi_ptr - buffer;
        int16_t rewind = offset - size + 1;

        if (eoi_ptr) {
            ESP_LOGD(TAG, "EOI found at offset: %d\n", offset);
            ESP_LOGD(TAG, "Rewind %d bytes\n", rewind);

            //fseek(fp, rewind, SEEK_CUR);
            lseek(fileno(fp), rewind, SEEK_CUR);
            bytes_read += rewind;
        }

        ESP_LOGD(TAG, "Read %d bytes\n", bytes_read);
        return bytes_read;
    } else {
        /* Skip bytes from input stream. */
        //bytes_skip = fseek(fp, size, SEEK_CUR) ? 0 : size;
        bytes_skip = 0;
        if (lseek(fileno(fp), size, SEEK_CUR) > 0) {
            bytes_skip = size;
        }

        ESP_LOGD(TAG, "Skipped %d bytes\n", bytes_read);
        return bytes_skip;
    }
}

static uint16_t tjpgd_data_writer(JDEC* decoder, void* bitmap, JRECT* rectangle)
{
    uint8_t width = (rectangle->right - rectangle->left) + 1;
    uint8_t height = (rectangle->bottom - rectangle->top) + 1;

    bitmap_t block = {
        .width = width,
        .height = height,
        .depth = DISPLAY_DEPTH,
        .pitch = width * (DISPLAY_DEPTH / 8),
        .size =  width * (DISPLAY_DEPTH / 8) * height,
        .buffer = (uint8_t *)bitmap
    };

    pod_blit(rectangle->left, rectangle->top + 30, &block);

    return 1;
}

/*
 * Read video data from sdcard. This should be capped to video
 * framerate. However currently the sdcard is the bottleneck and
 * data can be read at only about 12 fps.
 */
void video_task(void *params)
{
    TickType_t last;
    const TickType_t frequency = 1000 / 24 / portTICK_RATE_MS;

    last = xTaskGetTickCount();

    FILE *fp;
    uint8_t work[3100];
    JDEC decoder;
    JRESULT result;

    fp = fopen("/sdcard/bbb12.mjp", "rb");
    // setvbuf(fp, NULL, _IONBF, JD_SZBUF);

    if (!fp) {
        ESP_LOGE(TAG, "Unable to open file!");
    } else {
        ESP_LOGI(TAG, "Successfully opened file.");
    }

    while (1) {
        result = jd_prepare(&decoder, tjpgd_data_reader, work, 3100, fp);
        if (result == JDR_OK) {
            result = jd_decomp(&decoder, tjpgd_data_writer, 0);
            if (JDR_OK != result) {
                ESP_LOGE(TAG, "TJPGD decompress failed.");
            }
        } else {
            ESP_LOGE(TAG, "TJPGD prepare failed.");
        }
        sd_fps = fps();
        //vTaskDelayUntil(&last, frequency);
    }

    vTaskDelete(NULL);
}

/*
 * Displays the info bar on top of the screen.
 */
void infobar_task(void *params)
{
    uint16_t color = rgb565(0, 255, 0);
    char message[128];

#ifdef CONFIG_POD_HAL_USE_DOUBLE_BUFFERING
    while (1) {
        sprintf(message, "%.*f kBPS  ", 1, sd_bps / 1000);
        pod_put_text(message, 8, 8, color, font8x8);
        sprintf(message, "%.*f FPS  ", 1, sd_fps);
        pod_put_text(message, DISPLAY_WIDTH - 72, 8, color, font8x8);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
#endif
    vTaskDelete(NULL);
}

void photo_task(void *params)
{
    while (1) {
        pod_load_image(0, 30, "/sdcard/001.jpg");
        vTaskDelay(1000 / portTICK_RATE_MS);
        pod_load_image(0, 30, "/sdcard/002.jpg");
        vTaskDelay(1000 / portTICK_RATE_MS);
        pod_load_image(0, 30, "/sdcard/003.jpg");
        vTaskDelay(1000 / portTICK_RATE_MS);
        pod_load_image(0, 30, "/sdcard/004.jpg");
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void app_main()
{
    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    /* Save the backbuffer pointer so we can later read() directly into it. */
    bb = pod_init();
    if (bb) {
        ESP_LOGI(TAG, "Back buffer: %dx%dx%d", bb->width, bb->height, bb->depth);
    }

    sdcard_init();

    ESP_LOGI(TAG, "Heap after init: %d", esp_get_free_heap_size());

    mutex = xSemaphoreCreateMutex();

    if (NULL != mutex) {
#ifdef CONFIG_POD_HAL_USE_DOUBLE_BUFFERING
        xTaskCreatePinnedToCore(flush_task, "Flush", 8192, NULL, 1, NULL, 0);
        xTaskCreatePinnedToCore(video_task, "Video", 8192, NULL, 2, NULL, 1);
        //xTaskCreatePinnedToCore(photo_task, "Photo", 8192, NULL, 2, NULL, 1);
#endif
        xTaskCreatePinnedToCore(infobar_task, "info", 4096, NULL, 2, NULL, 1);
    } else {
        ESP_LOGE(TAG, "No mutex?");
    }
}
