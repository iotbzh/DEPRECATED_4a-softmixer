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

#define MAINLOOP_WATCHDOG 10000
#define MAX_AUDIO_STREAMS 8

// Provide proto for LibASound low level API
int _snd_pcm_dmix_open(snd_pcm_t **pcmp, const char *name,
		       snd_config_t *root, snd_config_t *conf,
		       snd_pcm_stream_t stream, int mode);

// alsa-ctl.c
PUBLIC snd_ctl_card_info_t *AlsaCtlGetInfo (CtlSourceT *source, const char *devid);
PUBLIC snd_ctl_t *AlsaCtlOpenCtl (CtlSourceT *source, const char *devid);
PUBLIC int AlsaCtlSubscribe(CtlSourceT *source, snd_ctl_t *ctlDev);
PUBLIC int AlsaCtlRegister(CtlSourceT *source, snd_pcm_t *pcm, int numid);

// alsa-tools-dump.c
PUBLIC void AlsaDumpFormats(CtlSourceT *source, snd_pcm_t *pcmHandle);
PUBLIC char *AlsaDumpPcmUid(snd_pcm_t *pcmHandle, char *buffer, size_t len);
PUBLIC void AlsaDumpCtlSubdev(CtlSourceT *source, snd_ctl_t *handle);
PUBLIC void AlsaDumpPcmInfo(CtlSourceT *source, snd_pcm_t *pcm, const char* info);
PUBLIC void AlsaDumpPcmParams(CtlSourceT *source, snd_pcm_hw_params_t *pcmHwParams);
PUBLIC void AlsaDumpCtlConfig(CtlSourceT *source, snd_config_t *config, int indent);
#define AlsaPcmUID(pcmHandle, buffer) AlsaDumpPcmUid(pcmHandle, buffer, sizeof(buffer))
PUBLIC char *AlsaDumpCtlUid(snd_ctl_t *ctlHandle, char *buffer, size_t len);
#define AlsaCtlUID(ctlHandle, buffer) AlsaDumpCtlUid(ctlHandle, buffer, sizeof(buffer))

// alsa-tools-bypath.c

typedef struct {
    char *devpath;
    char *devid;
    int device;
    int subdev;
    int numid;
} AlsaDevByPathT;

PUBLIC snd_ctl_card_info_t* AlsaByPathInfo (CtlSourceT *source, const char *control);
PUBLIC snd_pcm_t* AlsaByPathOpenPcm(CtlSourceT *source, AlsaDevByPathT *dev, snd_pcm_stream_t direction);
PUBLIC snd_ctl_t *AlsaByPathOpenCtl (CtlSourceT *source, AlsaDevByPathT *dev);
#define ALSA_PCM_DEFAULT_FORMAT (unsigned int)-99
#define ALSA_PCM_DEFAULT_ACCESS (snd_pcm_access_t)-99
#define ALSA_PCM_DEFAULT_RATE (snd_pcm_format_t)-99
#define ALSA_PCM_DEFAULT_CHANNELS (unsigned int)-99

// alsa-pcm.c
typedef struct {
    snd_pcm_format_t format;
    unsigned int channels;
    size_t sampleSize;
    unsigned int rate;
} AlsaPcmHwInfoT;

PUBLIC void AlsaPcmSetDefault (snd_pcm_format_t format, snd_pcm_access_t access, unsigned int rate, unsigned int channel);
PUBLIC int AlsaPcmConf(CtlSourceT *source, snd_pcm_t *pcmHandle, snd_pcm_format_t pcmFormat, unsigned int pcmRate, unsigned int pcmChannels, AlsaPcmHwInfoT *pcmHwInfo);
PUBLIC int AlsaPcmCopy(CtlSourceT *source, snd_pcm_t *pcmIn, snd_pcm_t *pcmOut, snd_pcm_format_t format, snd_pcm_access_t access, unsigned int rate, unsigned int channel);

// alse-dmix.c
PUBLIC snd_pcm_t* AlsaCreateDmix(CtlSourceT *source, const char *dmixName, const char *slaveName);
PUBLIC snd_pcm_t* AlsaCreateCapture(CtlSourceT *source, const char* sndDevPath, unsigned int deviceIdx, unsigned int subdevIdx, unsigned int channelCount);

#endif