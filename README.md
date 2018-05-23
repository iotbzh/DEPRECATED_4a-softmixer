Softmixer controller for 4A (AGL Advance Audio Architecture).
------------------------------------------------------------

 * Object: Simulate a hardware mixer through and Alsa-Loop driver and a user space mixer
 * Status: In Progress
 * Author: Fulup Ar Foll fulup@iot.bzh
 * Date  : April-2018

## Compile
```
    mkdir build
    cd build
    cmake ..
    make
```

## Install Alsa Loopback

You must have snd-aloop enabled a a module in you kernel.
Check that this way on target:

```
	zcat /proc/config.gz | grep CONFIG_SND_ALOOP
	CONFIG_SND_ALOOP=m
```

If it not the case, run menuconfig and enable it under
> Device Drivers > Sound card support > Advanced Linux Sound Architecture > Generic sound device


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

Current version does not handle audio rate conversion, using gstreamer or equivalent to match with audio hardware params is mandatory.
```
    gst123 --audio-output alsa=hw:XXX,0,7 $PROJECT_ROOT/conf.d/project/sounds/trio-divi-alkazabach.wav

    gst123 --audio-output alsa=hw:XXX,0,??? other sound file

    speaker-test -D hw:XXX:0:??? -twav -c!! 'cc' is the number of channel and depends on the audio stream zone target.
```

## Warning

Alsa try top automatically store current state into /var/lib/alsa/asound.state when developing/testing this may create impossible
situation. In order to clean up your Alsa snd-aloop config, a simple "rmmod" might not be enough in some case you may have to delete
/var/lib/alsa/asound.state before applying "modprobe".

In case of doubt check with folling command that you start from a clear green field
```
rmmod snd-aloop && modprobe --first-time  snd-aloop && amixer -D hw:Loopback controls | grep vol
```