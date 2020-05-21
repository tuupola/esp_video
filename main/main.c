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
#include <wchar.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

#include <unistd.h>

#include "sdkconfig.h"

#include <hagl_hal.h>
#include <hagl.h>
#include <bitmap.h>
#include <font6x9.h>
#include <fps.h>
#include <bps.h>
#include <sdcard.h>


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
        hagl_flush();
        xSemaphoreGive(mutex);
        //fb_fps = fps();
        vTaskDelayUntil(&last, frequency);
    }


    vTaskDelete(NULL);
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
    ssize_t bytes_read = 0;

    fp = fopen("/sdcard/bbb12.raw", "rb");

    if (!fp) {
        ESP_LOGE(TAG, "Unable to open file!");
    } else {
        ESP_LOGI(TAG, "Successfully opened file.");
    }

    while (1) {
        /* https://linux.die.net/man/3/read */
        bytes_read = read(
            fileno(fp),
            /* Center the video on 320 * 240 display */
            bb->buffer + bb->pitch * 30,
            320 * 180 * 2
        );
        sd_bps = bps(bytes_read);
        sd_fps = fps();
        vTaskDelayUntil(&last, frequency);
    }

    vTaskDelete(NULL);
}

/*
 * Displays the info bar on top of the screen.
 */
void infobar_task(void *params)
{
    uint16_t color = hagl_color(0, 255, 0);
    char16_t message[64];

#ifdef HAGL_HAL_USE_BUFFERING
    while (1) {
        swprintf(message, sizeof(message), u"SD %.*f kBPS  ",  1, sd_bps / 1000);
        hagl_put_text(message, 8, 8, color, font6x9);

        swprintf(message, sizeof(message), u"%.*f FPS  ", 1, sd_fps);
        hagl_put_text(message, DISPLAY_WIDTH - 62, 8, color, font6x9);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
#endif
    vTaskDelete(NULL);
}

void app_main()
{
    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    /* Save the backbuffer pointer so we can later read() directly into it. */
    bb = hagl_init();
    if (bb) {
        ESP_LOGI(TAG, "Back buffer: %dx%dx%d", bb->width, bb->height, bb->depth);
    }

    sdcard_init();

    ESP_LOGI(TAG, "Heap after init: %d", esp_get_free_heap_size());

    mutex = xSemaphoreCreateMutex();

    if (NULL != mutex) {
#ifdef HAGL_HAL_USE_BUFFERING
        xTaskCreatePinnedToCore(flush_task, "Flush", 8192, NULL, 1, NULL, 0);
        xTaskCreatePinnedToCore(video_task, "Video", 8192, NULL, 2, NULL, 1);
#endif
        xTaskCreatePinnedToCore(infobar_task, "info", 8192, NULL, 2, NULL, 1);
    } else {
        ESP_LOGE(TAG, "No mutex?");
    }
}
