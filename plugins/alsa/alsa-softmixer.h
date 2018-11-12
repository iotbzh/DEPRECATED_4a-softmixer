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

//#include <afb/afb-binding.h>
#include <json-c/json_object.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <systemd/sd-event.h>
#include <semaphore.h>

#include "ctl-plugin.h"
#include "wrap-json.h"

#include "alsa-ringbuf.h"

#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef STATIC
#define STATIC static
#endif

#define MAINLOOP_CONCURENCY 0
#define MAINLOOP_WATCHDOG 30000
#define ALSA_DEFAULT_PCM_RATE 48000
#define ALSA_DEFAULT_PCM_VOLUME 80

#define ALSA_CARDID_MAX_LEN 64


#define SMIXER_SUBDS_CTLS 3
#define SMIXER_DEFLT_LOOPS 4
#define SMIXER_DEFLT_SINKS 8
#define SMIXER_DEFLT_SOURCES 32
#define SMIXER_DEFLT_ZONES 32
#define SMIXER_DEFLT_STREAMS 32
#define SMIXER_DEFLT_RAMPS 8

#define ALSA_PLUG_PROTO(plugin) \
    int _snd_pcm_ ## plugin ## _open(snd_pcm_t **pcmp, const char *name, snd_config_t *root, snd_config_t *conf, snd_pcm_stream_t stream, int mode)

// auto switch from Log to API request depending on request presence.
#define AFB_IfReqFailF(mixer, request, status, format, ...) \
 if (request) AFB_ReqFailF(request, status, format, __VA_ARGS__); \
 else AFB_ApiError(mixer->api, format, __VA_ARGS__); 

#ifndef PUBLIC
#define PUBLIC
#endif
#ifndef STATIC
#define STATIC static
#endif

typedef enum {
    FONTEND_NUMID_IGNORE,
    FONTEND_NUMID_PAUSE,
    FONTEND_NUMID_RUN
} RegistryNumidT;

typedef struct {
    int cardidx;
    const char *devpath;
    const char *cardid;
    const char *name;
    const char *longname;
    const char *pcmplug_params;
    int device;
    int subdev;
} AlsaDevInfoT;

typedef struct {
    unsigned int rate;
    unsigned int channels;
    const char *formatS;
    snd_pcm_format_t format;
    snd_pcm_access_t access;
    size_t sampleSize;
} AlsaPcmHwInfoT;

typedef struct {
    int ccount;
    bool mute;
    int muteFd;
    AlsaDevInfoT cid;
    snd_pcm_t *handle;
    AlsaPcmHwInfoT *params;

    void * mixer;

    snd_pcm_uframes_t avail_min;
} AlsaPcmCtlT;

typedef struct {
	AlsaPcmCtlT *pcmIn;
	AlsaPcmCtlT *pcmOut;
    AFB_ApiT api;
    sd_event_source* evtsrc;

    size_t frame_size;
    snd_pcm_uframes_t latency;	/* final latency in frames */

    // IO Job
	alsa_ringbuf_t * rbuf;

	uint32_t		  write_err_count;
	uint32_t		  read_err_count;

    unsigned int channels;
    sd_event *sdLoop;

    pthread_t rthread;
    pthread_t wthread;

    int tid;
    char* info;

    int nbPcmFds;
    struct pollfd pollFds[2];

    sem_t sem;
    pthread_mutex_t mutex;

    int saveFd;

} AlsaPcmCopyHandleT;

typedef struct {
    const char*name;
    int numid;
    int count;
    long min;
    long max;
    long step;
} AlsaSndControlT;

typedef struct {
    const char*uid;
    int port;
} AlsaPcmChannelT;



typedef struct {
    const char *uid;
    int delay; // delay between volset in us
    int stepDown; // linear %
    int stepUp; // linear %
} AlsaVolRampT;





typedef struct {
    int numid;
    RegistryNumidT type;
    AlsaPcmCtlT *pcm;
} RegistryEntryPcmT;

typedef struct {
    long rcount;
    AlsaDevInfoT cid;
    snd_ctl_t *ctl;
    AlsaPcmHwInfoT *params;
    RegistryEntryPcmT **registry;
} AlsaSndCtlT;


typedef struct {
    const char *uid;
    AlsaPcmChannelT **sources;
    AlsaPcmChannelT **sinks;
    int ccount;
    AlsaPcmHwInfoT *params;
} AlsaSndZoneT;

typedef struct {
    const char *uid;
    const char *verb;
    unsigned int ccount;
    AlsaSndCtlT *sndcard;
    AlsaSndControlT volume;
    AlsaSndControlT mute;
    AlsaPcmChannelT **channels;
    snd_pcm_stream_t direction;
} AlsaSndPcmT;

typedef struct {
    const char*uid;
    int index; // AVIRT: parent PCM index (Since subdev idx is always 0)
    int numid;
} AlsaLoopSubdevT;

typedef struct {
    bool avirt; // AVIRT: Is this loop AVIRT?
    const char *uid;
    int playback; // AVIRT: UNUSED
    int capture; // AVIRT: UNUSED
    long scount; // AVIRT: PCM count
    AlsaSndCtlT *sndcard;
    AlsaLoopSubdevT **subdevs; // AVIRT: each subdev is on a different PCM
} AlsaSndLoopT;

typedef struct {
    const char *uid;
    const char *verb;
    const char *info;
    const char *sink;
    const char *source;
    const char *ramp;
    int volume;
    int mute;
    AlsaPcmHwInfoT *params;
    AlsaPcmCopyHandleT *copy;
} AlsaStreamAudioT;

typedef struct {
    const char *uid;
    const char *info;
    AFB_ApiT api;
    sd_event *sdLoop;

    struct {
        unsigned int loops;
        unsigned int sinks;
        unsigned int sources;
        unsigned int zones;
        unsigned int streams;
        unsigned int ramps;
    } max;
    AlsaSndLoopT **loops;
    AlsaSndPcmT **sinks;
    AlsaSndPcmT **sources;
    AlsaSndZoneT **zones;
    AlsaStreamAudioT **streams;
    AlsaVolRampT **ramps;
} SoftMixerT;

// alsa-utils-bypath.c
PUBLIC snd_ctl_card_info_t *AlsaByPathInfo(SoftMixerT *mixer, const char *devpath);
PUBLIC AlsaPcmCtlT *AlsaByPathOpenPcm(SoftMixerT *mixer, AlsaDevInfoT *pcmId, snd_pcm_stream_t direction);
PUBLIC snd_ctl_t *AlsaByPathOpenCtl(SoftMixerT *mixer, const char *uid, AlsaSndCtlT *dev);

// alsa-utils-dump.c
#define ALSA_PCM_UID(pcmHandle, buffer) AlsaDumpPcmUid(pcmHandle, buffer, sizeof(buffer))
#define ALSA_CTL_UID(ctlHandle, buffer) AlsaDumpCtlUid(ctlHandle, buffer, sizeof(buffer))
PUBLIC json_object *AlsaDumpObjF(const char *format, ...);
PUBLIC char *AlsaDumpPcmUid(snd_pcm_t *pcmHandle, char *buffer, size_t len);
PUBLIC char *AlsaDumpCtlUid(snd_ctl_t *ctlHandle, char *buffer, size_t len);
PUBLIC void AlsaDumpFormats(SoftMixerT *mixer, snd_pcm_t *pcmHandle);
PUBLIC void AlsaDumpCtlSubdev(SoftMixerT *mixer, snd_ctl_t *handle);
PUBLIC void AlsaDumpPcmParams(SoftMixerT *mixer, snd_pcm_hw_params_t *pcmHwParams);
PUBLIC void AlsaDumpPcmInfo(SoftMixerT *mixer, const char* info, snd_pcm_t *pcm);
PUBLIC void AlsaDumpElemConfig(SoftMixerT *mixer, const char* info, const char* elem);
PUBLIC void AlsaDumpCtlConfig(SoftMixerT *mixer, const char* info, snd_config_t *config, int indent);

// alsa-core-ctl.c
PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNumidElemId(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid) ;
PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNameElemId(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName) ;
PUBLIC snd_ctl_t *AlsaCtlOpenCtl(SoftMixerT *mixer, const char *cardid) ;
PUBLIC int CtlElemIdGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, snd_ctl_elem_id_t *elemId, long *value) ;
PUBLIC int CtlElemIdSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, snd_ctl_elem_id_t *elemId, long value) ;
PUBLIC snd_ctl_card_info_t *AlsaCtlGetCardInfo(SoftMixerT *mixer, const char *cardid) ;
PUBLIC int AlsaCtlNumidSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid, long value) ;
PUBLIC int AlsaCtlNumidGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid, long* value) ;
PUBLIC int AlsaCtlNameSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName, long value) ;
PUBLIC int AlsaCtlNameGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName, long* value) ;
PUBLIC int AlsaCtlCreateControl(SoftMixerT *mixer, AlsaSndCtlT *sndcard, char* ctlName, int ctlCount, int ctlMin, int ctlMax, int ctlStep, long value) ;
PUBLIC snd_ctl_t* AlsaCrlFromPcm(SoftMixerT *mixer, snd_pcm_t *pcm) ;
PUBLIC int AlsaCtlSubscribe(SoftMixerT *mixer, const char *uid, AlsaSndCtlT *sndcard) ;
PUBLIC int AlsaCtlRegister(SoftMixerT *mixer, AlsaSndCtlT *sndcard, AlsaPcmCtlT *pcmdev,  RegistryNumidT type, int numid);

// alsa-core-pcm.c
PUBLIC int AlsaPcmConf(SoftMixerT *mixer, AlsaPcmCtlT *pcm, int mode);
PUBLIC int AlsaPcmCopy(SoftMixerT *mixer, AlsaStreamAudioT *stream, AlsaPcmCtlT *pcmIn, AlsaPcmCtlT *pcmOut, AlsaPcmHwInfoT * opts);

// alsa-plug-*.c _snd_pcm_PLUGIN_open_ see macro ALSA_PLUG_PROTO(plugin)
PUBLIC int AlsaPcmCopy(SoftMixerT *mixer, AlsaStreamAudioT *streamAudio, AlsaPcmCtlT *pcmIn, AlsaPcmCtlT *pcmOut, AlsaPcmHwInfoT * opts);
PUBLIC int AlsaPcmCopyMuteSignal(SoftMixerT *mixer, AlsaPcmCtlT *pcmIn, bool mute);
PUBLIC AlsaPcmCtlT* AlsaCreateSoftvol(SoftMixerT *mixer, AlsaStreamAudioT *stream, char *slaveid, AlsaSndCtlT *sndcard, char* ctlName, int max, int open);
PUBLIC AlsaPcmCtlT* AlsaCreateRoute(SoftMixerT *mixer, AlsaSndZoneT *zone, int open);
PUBLIC AlsaPcmCtlT* AlsaCreateRate(SoftMixerT *mixer, const char* pcmName, AlsaPcmCtlT *pcmSlave, AlsaPcmHwInfoT *params, int open);
PUBLIC AlsaPcmCtlT* AlsaCreateDmix(SoftMixerT *mixer, const char* pcmName, AlsaSndPcmT *pcmSlave, int open);

// alsa-api-*
PUBLIC AlsaLoopSubdevT *ApiLoopFindSubdev(SoftMixerT *mixer, const char *streamUid, const char *targetUid, AlsaSndLoopT **loop);
PUBLIC int ApiLoopAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ, json_object *streamsJ);
PUBLIC AlsaPcmHwInfoT *ApiPcmSetParams(SoftMixerT *mixer, const char *uid, json_object *paramsJ);
PUBLIC AlsaSndPcmT *ApiPcmAttachOne(SoftMixerT *mixer, const char *uid, snd_pcm_stream_t direction, json_object *argsJ);
PUBLIC AlsaVolRampT *ApiRampGetByUid(SoftMixerT *mixer, const char *uid);
PUBLIC int ApiRampAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object *argsJ);
PUBLIC AlsaPcmHwInfoT *ApiSinkGetParamsByZone(SoftMixerT *mixer, const char *target);
PUBLIC int ApiSinkAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ);
PUBLIC AlsaSndPcmT  *ApiSinkGetByUid(SoftMixerT *mixer, const char *target);
PUBLIC AlsaSndCtlT *ApiSourceFindSubdev(SoftMixerT *mixer, const char *target);
PUBLIC int ApiSourceAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ);
PUBLIC int ApiStreamAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, const char *prefix, json_object * argsJ);
PUBLIC AlsaSndZoneT *ApiZoneGetByUid(SoftMixerT *mixer, const char *target);
PUBLIC int ApiZoneAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ);

// alsa-effect-ramp.c
PUBLIC AlsaVolRampT *ApiRampGetByUid(SoftMixerT *mixer, const char *uid);
PUBLIC int AlsaVolRampApply(SoftMixerT *mixer, AlsaSndCtlT *sndcard, AlsaStreamAudioT *stream, json_object *rampJ);

#endif
