/*
 * Copyright(C) 2018 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http : //www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License

for the specific language governing permissions and
 * limitations under the License.
 *
 * reference :
 * https://github.com/zonque/simple-alsa-loop/blob/master/loop.c
 * https://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html#a31
 *
 */

#define _GNU_SOURCE  // needed for vasprintf

#include "alsa-softmixer.h"
#include <pthread.h>
#include <sys/syscall.h>

typedef struct {
    AFB_ApiT api;
    sd_event_source* evtsrc;
    pthread_t thread;
    int tid;
    char* info;
    snd_ctl_t *ctlDev;
    sd_event *sdLoop;
} SubscribeHandleT;

typedef struct {
    AlsaPcmInfoT *pcm;
    int numid;
} SubStreamT;

typedef struct {
    SubStreamT stream[MAX_AUDIO_STREAMS + 1];
    int count;
} AudioStreamHandleT;

static AudioStreamHandleT AudioStreamHandle;

STATIC snd_ctl_elem_id_t *AlsaCtlGetElemId(CtlSourceT *source, snd_ctl_t* ctlDev, int numid) {
    char string[32];
    int error;
    int index;
    snd_ctl_elem_list_t *ctlList = NULL;
    snd_ctl_elem_id_t *elemId;

    snd_ctl_elem_list_alloca(&ctlList);

    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlElemIdGetInt [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    if ((error = snd_ctl_elem_list_alloc_space(ctlList, snd_ctl_elem_list_get_count(ctlList))) < 0) {
        AFB_ApiError(source->api, "AlsaCtlElemIdGetInt [%s] fail retrieve count", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // Fulup: do not understand why snd_ctl_elem_list should be call twice to get a valid ctlCount
    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlElemIdGetInt [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // loop on control to find the right one
    int ctlCount = snd_ctl_elem_list_get_used(ctlList);
    for (index = 0; index < ctlCount; index++) {

        if (numid == snd_ctl_elem_list_get_numid(ctlList, index)) {
            snd_ctl_elem_id_malloc(&elemId);
            snd_ctl_elem_list_get_id(ctlList, index, elemId);
            break;
        }
    }

    if (index == ctlCount) {
        AFB_ApiError(source->api, "AlsaCtlRegister [%s] fail get numid=%i count", ALSA_CTL_UID(ctlDev, string), numid);
        goto OnErrorExit;
    }

    // clear ctl list and return elemid
    snd_ctl_elem_list_clear(ctlList);
    return elemId;

OnErrorExit:
    if (ctlList) snd_ctl_elem_list_clear(ctlList);
    return NULL;
}


PUBLIC snd_ctl_t *AlsaCtlOpenCtl(CtlSourceT *source, const char *cardid) {
    int error;
    snd_ctl_t *ctlDev;

    if (cardid) goto OnErrorExit;

    if ((error = snd_ctl_open(&ctlDev, cardid, SND_CTL_READONLY)) < 0) {
        cardid = "Not Defined";
        goto OnErrorExit;
    }

    return ctlDev;

OnErrorExit:
    AFB_ApiError(source->api, "AlsaCtlOpenCtl: fail to find sndcard by id= %s", cardid);
    return NULL;
}

STATIC int CtlElemIdGetNumid(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, int *numid) {
    snd_ctl_elem_info_t *elemInfo;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(ctlDev, elemInfo) < 0) goto OnErrorExit;
    if (!snd_ctl_elem_info_is_readable(elemInfo)) goto OnErrorExit;

    *numid = snd_ctl_elem_info_get_numid(elemInfo);

    return 0;

OnErrorExit:
    return -1;
}

STATIC int CtlElemIdGetInt(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, long *value) {
    int error;
    snd_ctl_elem_value_t *elemData;
    snd_ctl_elem_info_t *elemInfo;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(ctlDev, elemInfo) < 0) goto OnErrorExit;
    if (!snd_ctl_elem_info_is_readable(elemInfo)) goto OnErrorExit;

    // as we have static rate/channel we should have only one boolean as value
    snd_ctl_elem_type_t elemType = snd_ctl_elem_info_get_type(elemInfo);
    int count = snd_ctl_elem_info_get_count(elemInfo);
    if (count != 1) goto OnErrorExit;

    snd_ctl_elem_value_alloca(&elemData);
    snd_ctl_elem_value_set_id(elemData, elemId);
    error = snd_ctl_elem_read(ctlDev, elemData);
    if (error) goto OnSuccessExit;

    // value=1 when active and 0 when not active
    *value = snd_ctl_elem_value_get_integer(elemData, 0);

OnSuccessExit:
    return 0;

OnErrorExit:

    AFB_ApiWarning(api, "CtlSubscribeEventCB: ignored unsupported event Numid=%i", snd_ctl_elem_info_get_numid(elemInfo));
    for (int idx = 0; idx < count; idx++) {
        long valueL;

        switch (elemType) {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
                valueL = snd_ctl_elem_value_get_boolean(elemData, idx);
                AFB_ApiNotice(api, "CtlElemIdGetBool: value=%ld", valueL);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER:
                valueL = snd_ctl_elem_value_get_integer(elemData, idx);
                AFB_ApiNotice(api, "CtlElemIdGetInt: value=%ld", valueL);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER64:
                valueL = snd_ctl_elem_value_get_integer64(elemData, idx);
                AFB_ApiNotice(api, "CtlElemIdGetInt64: value=%ld", valueL);
                break;
            case SND_CTL_ELEM_TYPE_ENUMERATED:
                valueL = snd_ctl_elem_value_get_enumerated(elemData, idx);
                AFB_ApiNotice(api, "CtlElemIdGetEnum: value=%ld", valueL);
                break;
            case SND_CTL_ELEM_TYPE_BYTES:
                valueL = snd_ctl_elem_value_get_byte(elemData, idx);
                AFB_ApiNotice(api, "CtlElemIdGetByte: value=%ld", valueL);
                break;
            case SND_CTL_ELEM_TYPE_IEC958:
            default:
                AFB_ApiNotice(api, "CtlElemIdGetInt: Unsupported type=%d", elemType);
                break;
        }
    }
    return -1;
}

// Clone of AlsaLib snd_card_load2 static function
PUBLIC snd_ctl_card_info_t *AlsaCtlGetInfo(CtlSourceT *source, const char *cardid) {
    int error;
    snd_ctl_t *ctlDev;

    if (cardid) goto OnErrorExit;

    if ((error = snd_ctl_open(&ctlDev, cardid, SND_CTL_READONLY)) < 0) {
        cardid = "Not Defined";
        goto OnErrorExit;
    }

    snd_ctl_card_info_t *cardInfo = malloc(snd_ctl_card_info_sizeof());
    if ((error = snd_ctl_card_info(ctlDev, cardInfo)) < 0) {
        goto OnErrorExit;
    }
    return cardInfo;

OnErrorExit:
    AFB_ApiError(source->api, "AlsaCtlGetInfo: fail to find sndcard by id= %s", cardid);
    return NULL;
}

PUBLIC int AlsaCtlGetNumidValueI(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long* value) {
     
    snd_ctl_elem_id_t *elemId = AlsaCtlGetElemId(source, ctlDev, numid);
    if (!elemId) {
        AFB_ApiError(source->api, "AlsaCtlGetNumValueI [sndcard=%s] fail to find numid=%d", snd_ctl_name(ctlDev), numid);
        goto OnErrorExit;
    }

    int error = CtlElemIdGetInt(source->api, ctlDev, elemId, value);
    if (error) {
        AFB_ApiError(source->api, "AlsaCtlGetNumValueI [sndcard=%s] fail to get numid=%d value", snd_ctl_name(ctlDev), numid);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

STATIC int CtlSubscribeEventCB(sd_event_source* src, int fd, uint32_t revents, void* userData) {
    int error, numid;
    SubscribeHandleT *subscribeHandle = (SubscribeHandleT*) userData;
    snd_ctl_event_t *eventId;
    snd_ctl_elem_id_t *elemId;
    long value;
    int idx;

    if ((revents & EPOLLHUP) != 0) {
        AFB_ApiNotice(subscribeHandle->api, "CtlSubscribeEventCB hanghup [card:%s disconnected]", subscribeHandle->info);
        goto OnSuccessExit;
    }

    if ((revents & EPOLLIN) == 0) goto OnSuccessExit;

    // initialise event structure on stack
    snd_ctl_event_alloca(&eventId);
    snd_ctl_elem_id_alloca(&elemId);

    error = snd_ctl_read(subscribeHandle->ctlDev, eventId);
    if (error < 0) goto OnErrorExit;

    // we only process sndctrl element
    if (snd_ctl_event_get_type(eventId) != SND_CTL_EVENT_ELEM) goto OnSuccessExit;

    // we only process value changed events
    unsigned int eventMask = snd_ctl_event_elem_get_mask(eventId);
    if (!(eventMask & SND_CTL_EVENT_MASK_VALUE)) goto OnSuccessExit;

    // extract element from event and get value    
    snd_ctl_event_elem_get_id(eventId, elemId);
    error = CtlElemIdGetInt(subscribeHandle->api, subscribeHandle->ctlDev, elemId, &value);
    if (error) goto OnErrorExit;

    error = CtlElemIdGetNumid(subscribeHandle->api, subscribeHandle->ctlDev, elemId, &numid);
    if (error) goto OnErrorExit;

    for (idx = 0; idx < AudioStreamHandle.count; idx++) {
        if (AudioStreamHandle.stream[idx].numid == numid) {
            const char *pcmName = AudioStreamHandle.stream[idx].pcm->cardid;
            snd_pcm_pause(AudioStreamHandle.stream[idx].pcm->handle, !value);
            AFB_ApiNotice(subscribeHandle->api, "CtlSubscribeEventCB:%s/%d pcm=%s pause=%d numid=%d", subscribeHandle->info, subscribeHandle->tid, pcmName, !value, numid);
            break;
        }
    }
    if (idx == AudioStreamHandle.count) {
        char cardName[32];
        ALSA_CTL_UID(subscribeHandle->ctlDev,cardName);
        AFB_ApiWarning(subscribeHandle->api, "CtlSubscribeEventCB:%s/%d card=%s numid=%d (ignored)", subscribeHandle->info, subscribeHandle->tid, cardName, numid);        
    }
    
OnSuccessExit:
    return 0;

OnErrorExit:
    AFB_ApiWarning(subscribeHandle->api, "CtlSubscribeEventCB: ignored unsupported event");
    return 0;
}

static void *LoopInThread(void *handle) {
    SubscribeHandleT *subscribeHandle = (SubscribeHandleT*) handle;
    int count = 0;
    int watchdog = MAINLOOP_WATCHDOG * 1000;
    subscribeHandle->tid = (int) syscall(SYS_gettid);

    AFB_ApiNotice(subscribeHandle->api, "LoopInThread:%s/%d Started", subscribeHandle->info, subscribeHandle->tid);

    /* loop until end */
    for (;;) {
        int res = sd_event_run(subscribeHandle->sdLoop, watchdog);
        if (res == 0) {
            AFB_ApiInfo(subscribeHandle->api, "LoopInThread:%s/%d Idle count=%d", subscribeHandle->info, subscribeHandle->tid, count++);
            continue;
        }
        if (res < 0) {
            AFB_ApiError(subscribeHandle->api, "LoopInThread:%s/%d ERROR=%i Exit errno=%s", subscribeHandle->info, subscribeHandle->tid, res, strerror(res));
            break;
        }
    }
    pthread_exit(0);
}

PUBLIC snd_ctl_t* AlsaCrlFromPcm(CtlSourceT *source, snd_pcm_t *pcm) {
    char buffer[32];
    int error;
    snd_ctl_t *ctlDev;
    snd_pcm_info_t *pcmInfo;

    snd_pcm_info_alloca(&pcmInfo);
    if ((error = snd_pcm_info(pcm, pcmInfo)) < 0) goto OnErrorExit;

    int pcmCard = snd_pcm_info_get_card(pcmInfo);
    snprintf(buffer, sizeof (buffer), "hw:%i", pcmCard);
    if ((error = snd_ctl_open(&ctlDev, buffer, SND_CTL_READONLY)) < 0) goto OnErrorExit;

    return ctlDev;

OnErrorExit:
    return NULL;
}


PUBLIC int AlsaCtlSubscribe(CtlSourceT *source, snd_ctl_t * ctlDev) {
    int error;
    char string [32];
    struct pollfd pfds;
    SubscribeHandleT *subscribeHandle = malloc(sizeof (SubscribeHandleT));

    subscribeHandle->api = source->api;
    subscribeHandle->ctlDev = ctlDev;
    subscribeHandle->info = "ctlEvt";

    // subscribe for sndctl events attached to cardid
    if ((error = snd_ctl_subscribe_events(ctlDev, 1)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlSubscribe: fail sndcard=%s to subscribe events", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // get pollfd attach to this sound board
    int count = snd_ctl_poll_descriptors(ctlDev, &pfds, 1);
    if (count != 1) {
        AFB_ApiError(source->api, "AlsaCtlSubscribe: fail sndcard=%s get poll descriptors", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // add poll descriptor to AGL systemd mainloop 
    if ((error = sd_event_new(&subscribeHandle->sdLoop)) < 0) {
        fprintf(stderr, "AlsaCtlSubscribe: fail  sndcard=%s creating a new loop", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // register sound event to binder main loop
    if ((error = sd_event_add_io(subscribeHandle->sdLoop, &subscribeHandle->evtsrc, pfds.fd, EPOLLIN, CtlSubscribeEventCB, subscribeHandle)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlSubscribe: Fail sndcard=%s adding mainloop", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // start a thread with a mainloop to monitor Audio-Agent
    if ((error = pthread_create(&subscribeHandle->thread, NULL, &LoopInThread, subscribeHandle)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlSubscribe: Fail  sndcard=%s create waiting thread err=%d", ALSA_CTL_UID(ctlDev, string), error);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlRegister(CtlSourceT *source, AlsaPcmInfoT *pcm, int numid) {
    long value;
    int error;

    // NumID are attached to sndcard retrieve ctldev from PCM
    snd_ctl_t* ctlDev = AlsaCrlFromPcm(source, pcm->handle);
    if (!ctlDev) {
        AFB_ApiError(source->api, "AlsaCtlRegister [pcm=%s] fail attache sndcard", pcm->cardid);
        goto OnErrorExit;
    }

    // This is the first registration let's subscrive to event
    if (AudioStreamHandle.count == 0) {
        AlsaCtlSubscribe(source, ctlDev);
    }

    error = AlsaCtlGetNumidValueI(source, ctlDev, numid, &value);
    if (error) goto OnErrorExit;

    AFB_ApiNotice(source->api, "AlsaCtlRegister [pcm=%s] numid=%d value=%ld", pcm->cardid, numid, value);

    // store PCM in order to pause/resume depending on event
    int count=AudioStreamHandle.count;
    AudioStreamHandle.stream[count].pcm = pcm;
    AudioStreamHandle.stream[count].numid = numid;

    // we only need to keep ctldev open for initial registration 
    if (AudioStreamHandle.count++ > 0) snd_ctl_close(ctlDev);

    // toggle pause/resume (should be done after pcm_start)
    if ((error = snd_pcm_pause(pcm->handle, !value)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlRegister [pcm=%s] fail to pause", pcm->cardid);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:

    return -1;
}
