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

#ifndef _ALSA_SOFTMIXER_
#define _ALSA_SOFTMIXER_

#include <afb/afb-binding.h>
#include <systemd/sd-event.h>
#include <json-c/json_object.h>
#include <stdio.h>

#include "ctl-plugin.h"
#include "wrap-json.h"

#include <alsa/asoundlib.h>

#define MAINLOOP_WATCHDOG 30000
#define MAX_AUDIO_STREAMS 8

#define ALSA_PLUG_PROTO(plugin) \
    int _snd_pcm_ ## plugin ## _open(snd_pcm_t **pcmp, const char *name, snd_config_t *root, snd_config_t *conf, snd_pcm_stream_t stream, int mode)

// alsa-utils-bypath.c

typedef struct {
    const char *devpath;
    const char *devid;
    int device;
    int subdev;
    int numid;
    int cardid;
    snd_pcm_t *handle;
} AlsaPcmInfoT;

typedef struct {
    unsigned int rate;
    unsigned int channels;
    snd_pcm_format_t format;
    snd_pcm_access_t access; 
    size_t sampleSize;
} AlsaPcmHwInfoT;

PUBLIC snd_ctl_card_info_t* AlsaByPathInfo (CtlSourceT *source, const char *control);
PUBLIC AlsaPcmInfoT* AlsaByPathOpenPcm(CtlSourceT *source, AlsaPcmInfoT *dev, snd_pcm_stream_t direction);
PUBLIC snd_ctl_t *AlsaByPathOpenCtl (CtlSourceT *source, AlsaPcmInfoT *dev);
PUBLIC int AlsaByPathDevid (CtlSourceT *source, AlsaPcmInfoT *dev);

// alsa-utils-dump.c
PUBLIC void AlsaDumpFormats(CtlSourceT *source, snd_pcm_t *pcmHandle);
PUBLIC char *AlsaDumpPcmUid(snd_pcm_t *pcmHandle, char *buffer, size_t len);
PUBLIC void AlsaDumpCtlSubdev(CtlSourceT *source, snd_ctl_t *handle);
PUBLIC void AlsaDumpPcmInfo(CtlSourceT *source, snd_pcm_t *pcm, const char* info);
PUBLIC void AlsaDumpPcmParams(CtlSourceT *source, snd_pcm_hw_params_t *pcmHwParams);
PUBLIC void AlsaDumpCtlConfig(CtlSourceT *source, snd_config_t *config, int indent);
#define ALSA_PCM_UID(pcmHandle, buffer) AlsaDumpPcmUid(pcmHandle, buffer, sizeof(buffer))
PUBLIC char *AlsaDumpCtlUid(snd_ctl_t *ctlHandle, char *buffer, size_t len);
#define ALSA_CTL_UID(ctlHandle, buffer) AlsaDumpCtlUid(ctlHandle, buffer, sizeof(buffer))

// alsa-core-ctl.c
PUBLIC snd_ctl_card_info_t *AlsaCtlGetInfo (CtlSourceT *source, const char *devid);
PUBLIC snd_ctl_t *AlsaCtlOpenCtl (CtlSourceT *source, const char *devid);
PUBLIC int AlsaCtlSubscribe(CtlSourceT *source, snd_ctl_t *ctlDev);
PUBLIC int AlsaCtlRegister(CtlSourceT *source, AlsaPcmInfoT *pcm, int numid);
PUBLIC int AlsaCtlGetNumidValueI(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long* value);



PUBLIC int AlsaPcmConf(CtlSourceT *source, AlsaPcmInfoT *pcm, AlsaPcmHwInfoT *opts);
PUBLIC int AlsaPcmCopy(CtlSourceT *source, AlsaPcmInfoT *pcmIn, AlsaPcmInfoT *pcmOut, AlsaPcmHwInfoT *opts);

// _snd_pcm_PLUGIN_open_ see macro ALSA_PLUG_PROTO(plugin)
PUBLIC AlsaPcmInfoT* AlsaCreateDmix(CtlSourceT *source, const char* pcmName, AlsaPcmInfoT *pcmSlave);
PUBLIC AlsaPcmInfoT* AlsaCreateVol(CtlSourceT *source, const char *pcmName, AlsaPcmInfoT* ctlTarget, AlsaPcmInfoT* pcmSlave);
#endif