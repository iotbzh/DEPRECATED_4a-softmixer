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
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <alsa/asoundlib.h>

#include "ctl-plugin.h"
#include "wrap-json.h"


#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef STATIC
#define STATIC static
#endif

#define MAINLOOP_WATCHDOG 30000
#define MAX_AUDIO_STREAMS 8*2
#define ALSA_DEFAULT_PCM_RATE 48000
#define ALSA_DEFAULT_PCM_VOLUME 80
#define ALSA_BUFFER_FRAMES_COUNT 1024
#define ALSA_CARDID_MAX_LEN 64


#define ALSA_PLUG_PROTO(plugin) \
    int _snd_pcm_ ## plugin ## _open(snd_pcm_t **pcmp, const char *name, snd_config_t *root, snd_config_t *conf, snd_pcm_stream_t stream, int mode)

#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef STATIC
#define STATIC static
#endif

// alsa-utils-bypath.c

typedef struct {
    const char*uid;
    int port;
} AlsaPcmChannels;

typedef struct {
    unsigned int rate;
    unsigned int channels;
    snd_pcm_format_t format;
    snd_pcm_access_t access;
    size_t sampleSize;
} AlsaPcmHwInfoT;

typedef struct {
    const char *uid;
    const char *devpath;
    const char *cardid;
    int cardidx;
    int device;
    int subdev;
    int numid;
    int ccount;
    snd_pcm_t *handle;
    AlsaPcmChannels *channels;
    AlsaPcmHwInfoT params;
} AlsaPcmInfoT;

typedef struct {
    const char *uid;
    snd_pcm_stream_t type;
    AlsaPcmChannels *channels;
    AlsaPcmInfoT *pcm;
} AlsaSndZoneT;

typedef struct {
    const char *uid;
    const char *devpath;
    const char *cardid;
    int cardidx;
    int playback;
    int capture;
    int scount;
    AlsaPcmInfoT *subdevs;
} AlsaSndLoopT;

typedef struct { 
    const char *uid;
    const char *info;
    const char *zone;
    int volume;
    int mute;
    AlsaPcmInfoT *pcm;
    AlsaPcmHwInfoT params;
} AlsaSndStreamT;

typedef struct {
    const char *uid;
    const char *info;
    AlsaSndLoopT *loop;
    AlsaPcmInfoT *backend;
    AlsaPcmInfoT *multiPcm;
    AlsaPcmInfoT **routes;
    AlsaSndStreamT *streams;
} SoftMixerHandleT; 

// alsa-utils-bypath.c
PUBLIC snd_ctl_card_info_t* AlsaByPathInfo(CtlSourceT *source, const char *control);
PUBLIC AlsaPcmInfoT* AlsaByPathOpenPcm(CtlSourceT *source, AlsaPcmInfoT *dev, snd_pcm_stream_t direction);
PUBLIC snd_ctl_t *AlsaByPathOpenCtl(CtlSourceT *source, AlsaPcmInfoT *dev);
PUBLIC int AlsaByPathDevid(CtlSourceT *source, AlsaPcmInfoT *dev);

// alsa-utils-dump.c
PUBLIC void AlsaDumpFormats(CtlSourceT *source, snd_pcm_t *pcmHandle);
PUBLIC char *AlsaDumpPcmUid(snd_pcm_t *pcmHandle, char *buffer, size_t len);
PUBLIC void AlsaDumpCtlSubdev(CtlSourceT *source, snd_ctl_t *handle);
PUBLIC void AlsaDumpElemConfig(CtlSourceT *source, const char* info, const char* elem);
PUBLIC void AlsaDumpPcmInfo(CtlSourceT *source, const char* info, snd_pcm_t *pcm);
PUBLIC void AlsaDumpPcmParams(CtlSourceT *source, snd_pcm_hw_params_t *pcmHwParams);
PUBLIC void AlsaDumpCtlConfig(CtlSourceT *source, const char* info, snd_config_t *config, int indent);
#define ALSA_PCM_UID(pcmHandle, buffer) AlsaDumpPcmUid(pcmHandle, buffer, sizeof(buffer))
PUBLIC char *AlsaDumpCtlUid(snd_ctl_t *ctlHandle, char *buffer, size_t len);
#define ALSA_CTL_UID(ctlHandle, buffer) AlsaDumpCtlUid(ctlHandle, buffer, sizeof(buffer))

// alsa-core-ctl.c
PUBLIC snd_ctl_card_info_t *AlsaCtlGetInfo(CtlSourceT *source, const char *cardid);
PUBLIC snd_ctl_t *AlsaCtlOpenCtl(CtlSourceT *source, const char *cardid);
PUBLIC snd_ctl_t* AlsaCrlFromPcm(CtlSourceT *source, snd_pcm_t *pcm);
PUBLIC int AlsaCtlSubscribe(CtlSourceT *source, snd_ctl_t *ctlDev);
PUBLIC int AlsaCtlRegister(CtlSourceT *source, AlsaPcmInfoT *pcm, int numid);
PUBLIC int AlsaCtlNumidGetLong(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long* value);
PUBLIC int AlsaCtlNumidSetLong(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long value);
PUBLIC int AlsaCtlNameGetLong(CtlSourceT *source, snd_ctl_t* ctlDev, const char *ctlName, long* value);
PUBLIC int AlsaCtlNameSetLong(CtlSourceT *source, snd_ctl_t* ctlDev, const char *ctlName, long value);
PUBLIC int AlsaCtlCreateControl(CtlSourceT *source, snd_ctl_t* ctlDev, AlsaPcmInfoT *subdevs, char* name, int ctlCount, int ctlMin, int ctlMax, int ctlStep, long value);
PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNameElemId(CtlSourceT *source, snd_ctl_t* ctlDev, const char *ctlName);
PUBLIC int CtlElemIdSetLong(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, long value);
PUBLIC int CtlElemIdGetLong(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, long *value);

// alsa-core-pcm.c
PUBLIC int AlsaPcmConf(CtlSourceT *source, AlsaPcmInfoT *pcm, AlsaPcmHwInfoT *opts);
PUBLIC int AlsaPcmCopy(CtlSourceT *source, AlsaPcmInfoT *pcmIn, AlsaPcmInfoT *pcmOut, AlsaPcmHwInfoT *opts);

// alsa-plug-*.c _snd_pcm_PLUGIN_open_ see macro ALSA_PLUG_PROTO(plugin)
PUBLIC AlsaPcmInfoT* AlsaCreateDmix(CtlSourceT *source, const char* pcmName, AlsaPcmInfoT *pcmSlave, int open);
PUBLIC AlsaPcmInfoT* AlsaCreateMulti(CtlSourceT *source, const char *pcmName, int open);
PUBLIC AlsaPcmInfoT* AlsaCreateRoute(CtlSourceT *source, AlsaSndZoneT *zone, int open);
PUBLIC AlsaPcmInfoT* AlsaCreateSoftvol(CtlSourceT *source, AlsaSndStreamT *stream, AlsaPcmInfoT *ctlControl, const char* ctlName, int max, int open);
PUBLIC AlsaPcmInfoT* AlsaCreateRate(CtlSourceT *source, const char* pcmName, AlsaPcmInfoT *pcmSlave, int open);

// alsa-api-*
PUBLIC int ProcessSndParams(CtlSourceT *source, const char* uid, json_object *paramsJ, AlsaPcmHwInfoT *params);
PUBLIC int SndFrontend (CtlSourceT *source, json_object *argsJ);
PUBLIC int SndBackend (CtlSourceT *source, json_object *argsJ);
PUBLIC int SndZones (CtlSourceT *source, json_object *argsJ);
PUBLIC int SndStreams(CtlSourceT *source, json_object *argsJ, json_object **responseJ);

#endif