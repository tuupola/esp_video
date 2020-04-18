## Videoplayer for ESP32

```
$ git clone git@github.com:tuupola/esp_video.git --recursive
$ cd esp_video
$ cp sdkconfig.ttgo-t4-v13 sdkconfig
$ make -j8 flash
```

If you have some other board or display run menuconfig yourself.

```
$ git clone git@github.com:tuupola/esp_video.git --recursive
$ cd esp_video
$ make menuconfig
$ make -j8 flash
```

