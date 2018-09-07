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
#include "alsa-bluez.h"

// extracted IOCTLs from <alsa/asoundlib.h>
#define _IOR_HACKED(type,nr,size)       _IOC(_IOC_READ,(type),(nr),size)
#define SNDRV_CTL_IOCTL_CARD_INFO(size) _IOR_HACKED('U', 0x01, size)


// Clone of AlsaLib snd_card_load2 static function

PUBLIC snd_ctl_card_info_t *AlsaByPathInfo(SoftMixerT *mixer, const char *devpath) {
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
    AFB_ApiError(mixer->api, "AlsaCardInfoByPath: fail to find sndcard by path= %s", devpath);
    return NULL;
}

PUBLIC AlsaPcmCtlT *AlsaByPathOpenPcm(SoftMixerT *mixer, AlsaDevInfoT *pcmDev, snd_pcm_stream_t direction) {
    int error;
    AlsaPcmCtlT *pcmCtl = calloc(1, sizeof (AlsaPcmCtlT));
    char *cardid = NULL;

    if (pcmDev->pcmplug_params) {
    	pcmDev->cardid = pcmDev->pcmplug_params;
    }

    if (!pcmDev->cardid) {
        if (pcmDev->subdev) {
            if (asprintf(&cardid, "hw:%i,%i,%i", pcmDev->cardidx, pcmDev->device, pcmDev->subdev) == -1)
                goto OnErrorExit;
        }
        else if (pcmDev->device) {
            if (asprintf(&cardid, "hw:%i,%i", pcmDev->cardidx, pcmDev->device) == -1)
                goto OnErrorExit;
        }
        else {
            if (asprintf(&cardid, "hw:%i", pcmDev->cardidx) == -1)
                goto OnErrorExit;
        }
        pcmDev->cardid = (const char*)cardid;
    }

    // inherit CID from pcmDev
    pcmCtl->cid.cardid = pcmDev->cardid;
    pcmCtl->cid.cardidx = pcmDev->cardidx;
    pcmCtl->cid.device = pcmDev->device;
    pcmCtl->cid.subdev = pcmDev->subdev;
    pcmCtl->cid.name=NULL;
    pcmCtl->cid.longname=NULL;

    AFB_ApiInfo(mixer->api,
                "%s OPEN PCM '%s', direction %s",
                __func__, pcmDev->cardid, direction==SND_PCM_STREAM_PLAYBACK?"playback":"capture");

    error = snd_pcm_open(&pcmCtl->handle, pcmCtl->cid.cardid, direction, SND_PCM_NONBLOCK);
    if (error < 0) {
        AFB_ApiError(mixer->api, "%s: fail openpcm cardid=%s error=%s", __func__, pcmCtl->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    return pcmCtl;

OnErrorExit:
    free(pcmCtl);
    free(cardid);
    return NULL;
}

PUBLIC snd_ctl_t *AlsaByPathOpenCtl(SoftMixerT *mixer, const char *uid, AlsaSndCtlT *dev) {
    int err;
    snd_ctl_t *handle;

    // get card info from /dev/snd/xxx if not use hw:x,x,x
    snd_ctl_card_info_t *cardInfo = NULL;

    if (dev->cid.devpath)
        cardInfo = AlsaByPathInfo(mixer, dev->cid.devpath);
    else if (dev->cid.cardid)
        cardInfo = AlsaCtlGetCardInfo(mixer, dev->cid.cardid);
    else if (dev->cid.pcmplug_params)
    	cardInfo = AlsaCtlGetCardInfo(mixer, dev->cid.pcmplug_params);

    if (!cardInfo) {
        AFB_ApiError(mixer->api,
                     "%s: uid=%s fail to find sndcard by path=%s id=%s",
                     __func__, uid, dev->cid.devpath, dev->cid.cardid);
        goto OnErrorExit;
    }

    // extract useful info from cardInfo handle
    dev->cid.devpath = NULL;
    dev->cid.cardidx = snd_ctl_card_info_get_card(cardInfo);
    dev->cid.name = strdup(snd_ctl_card_info_get_name(cardInfo));
    dev->cid.longname = strdup(snd_ctl_card_info_get_longname(cardInfo));

    // build a valid name and open sndcard
    if (asprintf((char**) &dev->cid.cardid, "hw:%i", dev->cid.cardidx) == -1)
        goto OnErrorExit;

    if ((err = snd_ctl_open(&handle, dev->cid.cardid, 0)) < 0) {
        AFB_ApiError(mixer->api,
                     "%s uid=%s sndcard open fail cardid=%s longname=%s error=%s",
                     __func__, uid, dev->cid.cardid, dev->cid.longname, snd_strerror(err));
        goto OnErrorExit;
    }

    AFB_ApiNotice(mixer->api,
                  "%s: uid=%s cardid=%s cardname=%s",
                  __func__, uid, dev->cid.cardid, dev->cid.longname);
    free(cardInfo);
    return handle;

OnErrorExit:
    return NULL;
}
