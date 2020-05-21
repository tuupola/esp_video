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
#include <freertos/event_groups.h>
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
#include <tjpgd.h>

static const char *TAG = "main";

static float sd_fps;
static float sd_bps;
static bitmap_t *bb;

static EventGroupHandle_t event;

static const uint8_t FRAME_LOADED = (1 << 0);

/*
 * Flush backbuffer to display always when new frame is loaded.
 */
void flush_task(void *params)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            event,
            FRAME_LOADED,
            pdTRUE,
            pdFALSE,
            40 / portTICK_PERIOD_MS
        );

        /* Flush only when FRAME_LOADED is set. */
        if ((bits & FRAME_LOADED) != 0 ) {
            hagl_flush();
        }
    }

    vTaskDelete(NULL);
}

/*
 * Read video data from sdcard. This should be capped to video
 * framerate. However currently the sd card is the bottleneck and
 * data can be read at only about 15 fps. Adding vsync causes
 * fps to drop to 14.
 */
void raw_video_task(void *params)
{
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

        /* Update counters. */
        sd_bps = bps(bytes_read);
        sd_fps = fps();

        /* Notify flush task that frame has been loaded. */
        xEventGroupSetBits(event, FRAME_LOADED);

        /* Add some leeway for flush so SD card does catch up. */
        ets_delay_us(5000);
    }

    vTaskDelete(NULL);
}

/*
 * TJPGD input function
 * http://www.elm-chan.org/fsw/tjpgd/en/input.html
 */

static uint16_t tjpgd_data_reader(JDEC *decoder, uint8_t *buffer, uint16_t size)
{
    FILE *fp = (FILE *)decoder->device;
    uint16_t bytes_read = 0;
    uint16_t bytes_skip = 0;
    const uint16_t EOI = 0xffd9;
    uint8_t *eoi_ptr;

    if (buffer) {
        /* Read bytes from input stream. */
        bytes_read = read(fileno(fp), buffer, size);

        sd_bps = bps(bytes_read);

        /* Search for EOI. */
        eoi_ptr = memmem(buffer, size, &EOI, 2);
        int16_t offset = eoi_ptr - buffer;
        int16_t rewind = offset - size + 1;

        if (eoi_ptr) {
            ESP_LOGD(TAG, "EOI found at offset: %d", offset);
            ESP_LOGD(TAG, "Rewind %d bytes", rewind);

            lseek(fileno(fp), rewind, SEEK_CUR);
            bytes_read += rewind;
        }

        ESP_LOGD(TAG, "Read %d bytes", bytes_read);
        return bytes_read;
    } else {
        /* Skip bytes from input stream. */
        bytes_skip = 0;
        if (lseek(fileno(fp), size, SEEK_CUR) > 0) {
            bytes_skip = size;
        }

        ESP_LOGD(TAG, "Skipped %d bytes", bytes_read);
        return bytes_skip;
    }
}

/*
 * TJPGD output function
 * http://www.elm-chan.org/fsw/tjpgd/en/output.html
 */
static uint16_t tjpgd_data_writer(JDEC* decoder, void* bitmap, JRECT* rectangle)
{
    uint8_t width = (rectangle->right - rectangle->left) + 1;
    uint8_t height = (rectangle->bottom - rectangle->top) + 1;

    /* Create a HAGL bitmap from uncompressed block. */
    bitmap_t block = {
        .width = width,
        .height = height,
        .depth = DISPLAY_DEPTH,
    };

    bitmap_init(&block, (uint8_t *)bitmap);

    /* Blit the block to the display. */
    hagl_blit(rectangle->left, rectangle->top + 30, &block);

    return 1;
}

/*
 * Read video data from sdcard. This should be capped to video
 * framerate. However currently the ESP32 is the bottleneck and
 * data can be uncompressed at about 8 fps.
 */
void mjpg_video_task(void *params)
{
    FILE *fp;
    uint8_t work[3100];
    JDEC decoder;
    JRESULT result;

    fp = fopen("/sdcard/bbb08.mjp", "rb");

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

        /* Update counters. */
        sd_fps = fps();

        /* Notify flush task that frame has been loaded. */
        xEventGroupSetBits(event, FRAME_LOADED);

        /* Add some leeway for flush so SD card does catch up. */
        ets_delay_us(5000);
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

#ifdef CONFIG_ESP_VIDEO_MJPG
        swprintf(message, sizeof(message), u"MJPG %.*f FPS  ", 1, sd_fps);
        hagl_put_text(message, DISPLAY_WIDTH - 90, 8, color, font6x9);
#else
        swprintf(message, sizeof(message), u"RAW RGB565 %.*f FPS  ", 1, sd_fps);
        hagl_put_text(message, DISPLAY_WIDTH - 124, 8, color, font6x9);
#endif
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
#endif
    vTaskDelete(NULL);
}

void photo_task(void *params)
{
    while (1) {
        ESP_LOGI(TAG, "Loading: %s", "/sdcard/001.jpg");
        hagl_load_image(0, 30, "/sdcard/001.jpg");
        xEventGroupSetBits(event, FRAME_LOADED);
        vTaskDelay(1000 / portTICK_RATE_MS);

        ESP_LOGI(TAG, "Loading: %s", "/sdcard/002.jpg");
        hagl_load_image(0, 30, "/sdcard/002.jpg");
        xEventGroupSetBits(event, FRAME_LOADED);
        vTaskDelay(1000 / portTICK_RATE_MS);

        ESP_LOGI(TAG, "Loading: %s", "/sdcard/003.jpg");
        hagl_load_image(0, 30, "/sdcard/003.jpg");
        xEventGroupSetBits(event, FRAME_LOADED);
        vTaskDelay(1000 / portTICK_RATE_MS);

        ESP_LOGI(TAG, "Loading: %s", "/sdcard/004.jpg");
        hagl_load_image(0, 30, "/sdcard/004.jpg");
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void app_main()
{
    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Heap when starting: %d", esp_get_free_heap_size());

    event = xEventGroupCreate();

    /* Save the backbuffer pointer so we can later read() directly into it. */
    bb = hagl_init();
    if (bb) {
        ESP_LOGI(TAG, "Back buffer: %dx%dx%d", bb->width, bb->height, bb->depth);
    }

    sdcard_init();

    ESP_LOGI(TAG, "Heap after init: %d", esp_get_free_heap_size());

#ifdef HAGL_HAL_USE_BUFFERING
    xTaskCreatePinnedToCore(flush_task, "Flush", 8192, NULL, 1, NULL, 0);
#ifdef CONFIG_ESP_VIDEO_MJPG
    xTaskCreatePinnedToCore(mjpg_video_task, "Video", 8192, NULL, 1, NULL, 0);
#else
    xTaskCreatePinnedToCore(raw_video_task, "Video", 8192, NULL, 1, NULL, 0);
#endif /* CONFIG_ESP_VIDEO_MJPG */
    //xTaskCreatePinnedToCore(photo_task, "Photo", 8192, NULL, 2, NULL, 1);
#endif /* HAGL_HAL_USE_BUFFERING */
    xTaskCreatePinnedToCore(infobar_task, "info", 8192, NULL, 2, NULL, 1);
}
