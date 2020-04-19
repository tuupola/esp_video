## Videoplayer for ESP32

This is a proof of concept videoplayer which play raw RGB565 files from the SD card. You can convert any video file to correct format with ffmpeg. Currently videos are played without sound.

### Configure and compile

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

### Download and convert video

I used [Big Buck Bunny](https://peach.blender.org/download/) in the examples. Converted video should be copied to a FAT formatted SD card. Note that by default ESP-IDF does not support long filenames. Either enable them from `menuconfig` or use short 8.3 filenames.

```
$ wget https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4 -O bbb.mp4
$ ffmpeg -i BigBuckBunny_320x180.mp4 -f rawvideo -pix_fmt rgb565be -vcodec rawvideo bbb24.raw
```

The original video is 24 fps. With SPI interface the SD card reading speed seems to be the bottleneck. You can create 12 fps version with the following.

```
$ ffmpeg -i BigBuckBunny_320x180.mp4 -f rawvideo -pix_fmt rgb565be -vcodec  rawvideo -r 12 bbb12.raw
```





