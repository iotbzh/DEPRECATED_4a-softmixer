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
    snd_ctl_t *ctlDev;
} AudioStreamHandleT;

static AudioStreamHandleT AudioStreamHandle;


PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNumidElemId(CtlSourceT *source, snd_ctl_t* ctlDev, int numid) {
    char string[32];
    int error;
    int index;
    snd_ctl_elem_list_t *ctlList = NULL;
    snd_ctl_elem_id_t *elemId;

    snd_ctl_elem_list_alloca(&ctlList);

    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNumidElemId [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    if ((error = snd_ctl_elem_list_alloc_space(ctlList, snd_ctl_elem_list_get_count(ctlList))) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNumidElemId [%s] fail retrieve count", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // Fulup: do not understand why snd_ctl_elem_list should be call twice to get a valid ctlCount
    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNumidElemId [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
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
        AFB_ApiError(source->api, "AlsaCtlGetNumidElemId [%s] fail get numid=%i count", ALSA_CTL_UID(ctlDev, string), numid);
        goto OnErrorExit;
    }

    // clear ctl list and return elemid
    snd_ctl_elem_list_clear(ctlList);
    return elemId;

OnErrorExit:
    if (ctlList) snd_ctl_elem_list_clear(ctlList);
    return NULL;
}

PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNameElemId(CtlSourceT *source, snd_ctl_t* ctlDev, const char *ctlName) {
    char string[32];
    int error;
    int index;
    snd_ctl_elem_list_t *ctlList = NULL;
    snd_ctl_elem_id_t *elemId;

    snd_ctl_elem_list_alloca(&ctlList);

    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNameElemId [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    if ((error = snd_ctl_elem_list_alloc_space(ctlList, snd_ctl_elem_list_get_count(ctlList))) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNameElemId [%s] fail retrieve count", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // Fulup: do not understand why snd_ctl_elem_list should be call twice to get a valid ctlCount
    if ((error = snd_ctl_elem_list(ctlDev, ctlList)) < 0) {
        AFB_ApiError(source->api, "AlsaCtlGetNameElemId [%s] fail retrieve controls", ALSA_CTL_UID(ctlDev, string));
        goto OnErrorExit;
    }

    // loop on control to find the right one
    int ctlCount = snd_ctl_elem_list_get_used(ctlList);
    for (index = 0; index < ctlCount; index++) {

        if (!strcasecmp(ctlName, snd_ctl_elem_list_get_name(ctlList, index))) {
            snd_ctl_elem_id_malloc(&elemId);
            snd_ctl_elem_list_get_id(ctlList, index, elemId);
            break;
        }
    }

    if (index == ctlCount) {
        AFB_ApiError(source->api, "AlsaCtlGetNameElemId [%s] fail get ctl name=%s", ALSA_CTL_UID(ctlDev, string), ctlName);
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

STATIC void CtlElemIdDisplay(AFB_ApiT api, snd_ctl_elem_info_t *elemInfo, snd_ctl_elem_value_t *elemData) {

    int numid = snd_ctl_elem_info_get_numid(elemInfo);
    int count = snd_ctl_elem_info_get_count(elemInfo);
    const char* name = snd_ctl_elem_info_get_name(elemInfo);
    snd_ctl_elem_type_t elemType = snd_ctl_elem_info_get_type(elemInfo);


    if (!elemData) {
        AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=unreadable", numid, name);
    } else
        for (int idx = 0; idx < count; idx++) {
            long valueL;

            switch (elemType) {
                case SND_CTL_ELEM_TYPE_BOOLEAN:
                    valueL = snd_ctl_elem_value_get_boolean(elemData, idx);
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_INTEGER:
                    valueL = snd_ctl_elem_value_get_integer(elemData, idx);
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_INTEGER64:
                    valueL = snd_ctl_elem_value_get_integer64(elemData, idx);
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_ENUMERATED:
                    valueL = snd_ctl_elem_value_get_enumerated(elemData, idx);
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_BYTES:
                    valueL = snd_ctl_elem_value_get_byte(elemData, idx);
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_IEC958:
                default:
                    AFB_ApiWarning(api, "CtlElemIdDisplay: numid=%d name=%s Unsupported type=%d", numid, name, elemType);
                    break;
            }
        }
}

PUBLIC int CtlElemIdGetLong(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, long *value) {
    int error;
    snd_ctl_elem_value_t *elemData;
    snd_ctl_elem_info_t *elemInfo;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(ctlDev, elemInfo) < 0) goto OnErrorExit;
    if (!snd_ctl_elem_info_is_readable(elemInfo)) goto OnErrorExit;

    // as we have static rate/channel we should have only one boolean as value

    snd_ctl_elem_value_alloca(&elemData);
    snd_ctl_elem_value_set_id(elemData, elemId);
    error = snd_ctl_elem_read(ctlDev, elemData);
    if (error) {
        elemData = NULL;
        goto OnErrorExit;
    }

    // warning multi channel are always view as grouped
    //int count = snd_ctl_elem_info_get_count(elemInfo);
    //if (count != 1) goto OnErrorExit;

    // value=1 when active and 0 when not active
    *value = (int) snd_ctl_elem_value_get_integer(elemData, 0);

    return 0;

OnErrorExit:
    CtlElemIdDisplay(api, elemInfo, elemData);
    return -1;
}

PUBLIC int CtlElemIdSetLong(AFB_ApiT api, snd_ctl_t *ctlDev, snd_ctl_elem_id_t *elemId, long value) {
    snd_ctl_elem_value_t *elemData;
    snd_ctl_elem_info_t *elemInfo;
    const char* name;
    int error, numid;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(ctlDev, elemInfo) < 0) goto OnErrorExit;

    if (!snd_ctl_elem_info_is_writable(elemInfo)) goto OnErrorExit;

    int count = snd_ctl_elem_info_get_count(elemInfo);
    if (count == 0) goto OnErrorExit;

    snd_ctl_elem_value_alloca(&elemData);
    snd_ctl_elem_value_set_id(elemData, elemId);
    error = snd_ctl_elem_read(ctlDev, elemData);
    if (error) goto OnErrorExit;


    for (int index = 0; index < count; index++) {
        snd_ctl_elem_value_set_integer(elemData, index, value);
    }

    error = snd_ctl_elem_write(ctlDev, elemData);
    if (error) goto OnErrorExit;

    return 0;

OnErrorExit:
    numid = snd_ctl_elem_info_get_numid(elemInfo);
    name = snd_ctl_elem_info_get_name(elemInfo);
    AFB_ApiError(api, "CtlElemIdSetInt: numid=%d name=%s not writable", numid, name);
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

PUBLIC int AlsaCtlNumidSetLong(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNumidElemId(source, ctlDev, numid);
    if (!elemId) {
        AFB_ApiError(source->api, "AlsaCtlNumidSetLong [sndcard=%s] fail to find numid=%d", snd_ctl_name(ctlDev), numid);
        goto OnErrorExit;
    }

    int error = CtlElemIdSetLong(source->api, ctlDev, elemId, value);
    if (error) {
        AFB_ApiError(source->api, "AlsaCtlNumidSetLong [sndcard=%s] fail to set numid=%d value=%ld", snd_ctl_name(ctlDev), numid, value);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlNumidGetLong(CtlSourceT *source, snd_ctl_t* ctlDev, int numid, long* value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNumidElemId(source, ctlDev, numid);
    if (!elemId) {
        AFB_ApiError(source->api, "AlsaCtlGetNumValueI [sndcard=%s] fail to find numid=%d", snd_ctl_name(ctlDev), numid);
        goto OnErrorExit;
    }

    int error = CtlElemIdGetLong(source->api, ctlDev, elemId, value);
    if (error) {
        AFB_ApiError(source->api, "AlsaCtlGetNumValueI [sndcard=%s] fail to get numid=%d value", snd_ctl_name(ctlDev), numid);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

STATIC int AlsaCtlMakeControl(CtlSourceT *source, snd_ctl_t* ctlDev, AlsaPcmInfoT *subdev, const char *ctlName, int ctlCount, int ctlMin, int ctlMax, int ctlStep) {
    snd_ctl_elem_type_t ctlType;
    snd_ctl_elem_info_t *elemInfo;
    int error;

    snd_ctl_elem_info_alloca(&elemInfo);
    if (ctlName) snd_ctl_elem_info_set_name(elemInfo, ctlName);
    snd_ctl_elem_info_set_interface(elemInfo, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_info(ctlDev, elemInfo);
    
    // softvol plugin is bugged and can only map volume to sndcard device+subdev=0
    // snd_ctl_elem_info_set_device(elemInfo, subdev->device);
    // snd_ctl_elem_info_set_subdevice(elemInfo, subdev->subdev);
    snd_ctl_elem_info_set_device(elemInfo, 0);
    snd_ctl_elem_info_set_subdevice(elemInfo, 0);

    // only two types implemented
    if (ctlMin == 0 && ctlMax == 1) ctlType = SND_CTL_ELEM_TYPE_BOOLEAN;
    else ctlType = SND_CTL_ELEM_TYPE_INTEGER;

    switch (ctlType) {
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            error = snd_ctl_add_boolean_elem_set(ctlDev, elemInfo, 1, ctlCount);
            if (error) goto OnErrorExit;
            break;

        case SND_CTL_ELEM_TYPE_INTEGER:
            error = snd_ctl_add_integer_elem_set(ctlDev, elemInfo, 1, ctlCount, ctlMin, ctlMax, ctlStep);
            if (error) goto OnErrorExit;
            break;

        default:
            AFB_ApiError(source->api, "AlsaCtlMakeControl:%s(subdev) fail to create %s(control)", subdev->uid, ctlName);
            goto OnErrorExit;
    }

    // retrieve newly created control numid
    int numid = snd_ctl_elem_info_get_numid(elemInfo);
    return numid;

OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlCreateControl(CtlSourceT *source, snd_ctl_t* ctlDev, AlsaPcmInfoT *subdevs, char* ctlName, int ctlCount, int ctlMin, int ctlMax, int ctlStep, long value) {
    int numid = -1;

    // if control does not exist then create
    snd_ctl_elem_id_t *elemId = AlsaCtlGetNameElemId(source, ctlDev, ctlName);
    if (elemId) {
        numid = snd_ctl_elem_id_get_numid(elemId);
    } else {
        // create or get numid control when already exist
        numid = AlsaCtlMakeControl(source, ctlDev, subdevs, ctlName, ctlCount, ctlMin, ctlMax, ctlStep);
        if (numid <= 0) {
            AFB_ApiError(source->api, "AlsaCtlCreateControl [sndcard=%s] fail to create ctlName=%s", snd_ctl_name(ctlDev), ctlName);
            goto OnErrorExit;
        }

        elemId = AlsaCtlGetNumidElemId(source, ctlDev, numid);
    }

    int error = CtlElemIdSetLong(source->api, ctlDev, elemId, value);
    if (error) {
        AFB_ApiError(source->api, "AlsaCtlCreateControl [sndcard=%s] fail to set ctlName=%s Numid=%d", snd_ctl_name(ctlDev), ctlName, numid);
        goto OnErrorExit;
    }

    return numid;
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
    error = CtlElemIdGetLong(subscribeHandle->api, subscribeHandle->ctlDev, elemId, &value);
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
        ALSA_CTL_UID(subscribeHandle->ctlDev, cardName);
        AFB_ApiWarning(subscribeHandle->api, "CtlSubscribeEventCB:%s/%d card=%s numid=%d (ignored)", subscribeHandle->info, subscribeHandle->tid, cardName, numid);
    }

OnSuccessExit:
    return 0;

OnErrorExit:
    AFB_ApiInfo(subscribeHandle->api, "CtlSubscribeEventCB: ignored unsupported event");
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


    int count = AudioStreamHandle.count;
    if (count > MAX_AUDIO_STREAMS) {
        AFB_ApiError(source->api, "AlsaCtlRegister [pcm=%s] to many audio stream max=%d", pcm->cardid, MAX_AUDIO_STREAMS);
        goto OnErrorExit;
    }

    // If 1st registration then open a dev control channel to recieve events
    if (!AudioStreamHandle.ctlDev) {
        snd_ctl_t* ctlDev = AlsaCrlFromPcm(source, pcm->handle);
        if (!ctlDev) {
            AFB_ApiError(source->api, "AlsaCtlRegister [pcm=%s] fail attache sndcard", pcm->cardid);
            goto OnErrorExit;
        }

        AlsaCtlSubscribe(source, ctlDev);
    }

    // store PCM in order to pause/resume depending on event
    AudioStreamHandle.stream[count].pcm = pcm;
    AudioStreamHandle.stream[count].numid = numid;
    AudioStreamHandle.count++;

    return 0;

OnErrorExit:
    return -1;
}
