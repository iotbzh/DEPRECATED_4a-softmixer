Softmixer controller for 4A (AGL Advance Audio Architecture).
------------------------------------------------------------

 * Object: Simulate a hardware mixer through and snd-aloop driver and a user space mixer
 * Status: In Progress
 * Author: Fulup Ar Foll fulup@iot.bzh
 * Author: Thierry Bultel thierry.bultel@iot.bzh
 * Date  : July-2018

## Features
 * Definition of multiple audio zones, and multiple audio cards
 * Usage of streams to access audio zones
 * Direct read from HW capture devices (eg radio chips & microphones)

## Compile
```
    mkdir build
    cd build
    cmake ..
    make
```

## Install Alsa Loopback Driver

```
    sudo modprobe snd-aloop
```

## Assert LUA config file match your config 

```
    vim $PROJECT_ROOT/conf.d/project/lua.d/softmixer-01.lua

    # make sure both your loopback and targeted sound card path are valid
```


## Run from shell

```
    afb-daemon --name 4a-softmixer-afbd --port=1234 --workdir=/home/fulup/Workspace/Audio-4a/4a-softmixer/build \
               --binding=package/lib/softmixer-binding.so --roothttp=package/htdocs --token= --tracereq=common --verbose

    # lua test script should return a response looking like
    response= {
      [1] = { ["uid"] = navigation,["runid"] = 101,["alsa"] = hw:5,0,6,["volid"] = 103,}
            , ["params"] = { ["channels"] = 2,["format"] = 2,["rate"] = 48000,["access"] = 3,} 
     ,[2] = {  ....
    }

    # runid: pause/resume alsa control you may change it with 'amixer -D hw:Loopback cset numid=101 on|off
    # volid: volume alsa control you may change it from 'alsamixer -Dhw:Loppback' or with 'amixer -D hw:Loopback cset numid=103 NN (o-100%)
```

Retrieve audio-stream alsa endpoint from response to 'L2C:snd_streams' command. Depending on your config 'hw:XXX' will change. 
Alsa snd-aloop impose '0' as playback device. Soft mixer will start from last subdevice and allocates one subdev for each audio-stream.


## Play some music

snd-aloop only supports these audio formats:

S16_LE
S16_BE
S32_LE
S32_BE
FLOAT_LE
FLOAT_BE

Using gstreamer is a simple way to perform the conversion, if your audio file is not of one of these.

```
    gst-launch-1.0 filesrc location=insane.wav ! wavparse ! audioconvert ! audioresample ! alsasink device=hw:Loopback,0,2

```

## Warning

Alsa tries to automatically store current state into /var/lib/alsa/asound.state when developing/testing this may create impossible
situation. In order to clean up your Alsa snd-aloop config, a simple "rmmod" might not be enough in some case you may have to delete
/var/lib/alsa/asound.state before applying "modprobe".

In case of doubt check with folling command that you start from a clear green field
```
rmmod snd-aloop && modprobe --first-time  snd-aloop && amixer -D hw:Loopback controls | grep vol
```


## Work in Progress 
 * Support capture from bluez

## Known issues
 * From times to times, playing an audio file make the sound output with higher	pitch
 * The playback loop could be improved, using direct access to the ring buffer instead 
   of copies to stack (could save some CPU time)

