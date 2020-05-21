## Videoplayer for ESP32

This is a proof of concept videoplayer which plays raw RGB565 and MJPG video files from the SD card. You can convert any video file to raw format with ffmpeg. Currently videos are played without sound.

![Big Buck Bunny on TTGO T4](https://appelsiini.net/img/2020/bbb-cover-1.jpg)

You can also see how it works in [Vimeo](https://vimeo.com/409435420).

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

The original video is 24 fps. With SPI interface the SD card reading speed seems to be the bottleneck. You can create 12 fps raw video version with the following.

```
$ ffmpeg -i BigBuckBunny_320x180.mp4 -f rawvideo -pix_fmt rgb565be -vcodec  rawvideo -r 12 bbb12.raw
```

With MJPG compressed video ESP32 itself is the bottleneck. With my testing I
was able to decode at approximately 8 fps. You can create a 8 fps motion jpeg video with the following.

```
$ ffmpeg -i BigBuckBunny_320x180.mp4 -r 8 -an -f mjpeg -q:v 1 -pix_fmt yuvj420p -vcodec mjpeg -force_duplicated_matrix 1 -huffman 0 bbb08.mjpeg
```

There is a reasonable size difference between raw and motion jpeg files.

```
$ du -h bbb12.raw
798M	bbb12.raw

$ du -h bbb12.mjpeg
117M	bbb12.mjpeg
```

## Big Buck Bunny

Copyright (C) 2008 Blender Foundation | peach.blender.org<br>
Some Rights Reserved. Creative Commons Attribution 3.0 license.<br>
http://www.bigbuckbunny.org/
