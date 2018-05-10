/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#define _GNU_SOURCE  // needed for vasprintf

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "alsa-softmixer.h"

// extracted IOCTLs from <alsa/asoundlib.h>
#define _IOR_HACKED(type,nr,size)       _IOC(_IOC_READ,(type),(nr),size)
#define SNDRV_CTL_IOCTL_CARD_INFO(size) _IOR_HACKED('U', 0x01, size)


// Clone of AlsaLib snd_card_load2 static function

PUBLIC snd_ctl_card_info_t *AlsaByPathInfo(CtlSourceT *source, const char *devpath) {
    int open_dev;
    snd_ctl_card_info_t *cardInfo = malloc(snd_ctl_card_info_sizeof());

    if (!devpath) goto OnErrorExit;

    open_dev = open(devpath, O_RDONLY);
    if (open_dev < 0) goto OnErrorExit;

    int rc = ioctl(open_dev, SNDRV_CTL_IOCTL_CARD_INFO(snd_ctl_card_info_sizeof()), cardInfo);
    if (rc < 0) {
        close(open_dev);
        goto OnErrorExit;
    }

    close(open_dev);
    return cardInfo;

OnErrorExit:
    AFB_ApiError(source->api, "AlsaCardInfoByPath: fail to find sndcard by path= %s", devpath);
    return NULL;
}

PUBLIC int AlsaByPathDevid(CtlSourceT *source, AlsaPcmInfoT *dev) {

    // get card info from /dev/snd/xxx if not use hw:x,x,x
    snd_ctl_card_info_t *cardInfo = NULL;
    if (dev->devpath) cardInfo = AlsaByPathInfo(source, dev->devpath);
    else if (dev->devid) cardInfo = AlsaCtlGetInfo(source, dev->devid);

    if (!cardInfo) {
        AFB_ApiWarning(source->api, "AlsaByPathOpenPcm: fail to find sndcard by path=%s id=%s", dev->devpath, dev->devid);
        goto OnErrorExit;
    }

    // extract useful info from cardInfo handle
    dev->cardid = snd_ctl_card_info_get_card(cardInfo);

    // if not provided build a valid PCM devid 
    if (!dev->devid) {
        #define DEVID_MAX_LEN 32
        dev->devid=malloc(DEVID_MAX_LEN);  
        if (dev->subdev) snprintf((char*)dev->devid, DEVID_MAX_LEN, "hw:%i,%i,%i", dev->cardid, dev->device, dev->subdev);
        else if (dev->device) snprintf((char*)dev->devid, DEVID_MAX_LEN, "hw:%i,%i", dev->cardid, dev->device);
        else snprintf((char*)dev->devid, DEVID_MAX_LEN, "hw:%i", dev->cardid);
    }
    return 0;

OnErrorExit:
    return -1;
}

PUBLIC AlsaPcmInfoT* AlsaByPathOpenPcm(CtlSourceT *source, AlsaPcmInfoT *dev, snd_pcm_stream_t direction) {
    int error;

    error = AlsaByPathDevid(source, dev);
    if (error) goto OnErrorExit;

    error = snd_pcm_open(&dev->handle, dev->devid, direction, SND_PCM_NONBLOCK);
    if (error) {
        AFB_ApiError(source->api, "AlsaByPathOpenPcm: fail openpcm (devid=%s idxdev=%i subdev=%d): %s"
                , dev->devid, dev->device, dev->subdev, snd_strerror(error));
        goto OnErrorExit;
    }

    return (dev);

OnErrorExit:
    return NULL;
}

PUBLIC snd_ctl_t *AlsaByPathOpenCtl(CtlSourceT *source, AlsaPcmInfoT *dev) {
    int err;
    char devid[32];
    snd_ctl_t *handle;

    // get card info from /dev/snd/xxx if not use hw:x,x,x
    snd_ctl_card_info_t *cardInfo = NULL;
    if (dev->devpath) cardInfo = AlsaByPathInfo(source, dev->devpath);
    else if (dev->devid) cardInfo = AlsaCtlGetInfo(source, dev->devid);

    if (!cardInfo) {
        AFB_ApiError(source->api, "AlsaByPathOpenCtl: fail to find sndcard by path=%s id=%s", dev->devpath, dev->devid);
        goto OnErrorExit;
    }

    // extract useful info from cardInfo handle
    int cardIndex = snd_ctl_card_info_get_card(cardInfo);
    const char *cardId = snd_ctl_card_info_get_id(cardInfo);
    const char *cardName = snd_ctl_card_info_get_name(cardInfo);

    // build a valid name and open sndcard
    snprintf(devid, sizeof (devid), "hw:%i", cardIndex);
    if ((err = snd_ctl_open(&handle, devid, 0)) < 0) {
        AFB_ApiError(source->api, "control open (hw:%d -> %s): %s", cardIndex, cardName, snd_strerror(err));
        goto OnErrorExit;
    }

    AFB_ApiNotice(source->api, "AlsaCtlOpenByPath: sndcard hw:%d id=%s name=%s", cardIndex, cardId, cardName);
    free(cardInfo);
    return handle;

OnErrorExit:
    return NULL;
}
