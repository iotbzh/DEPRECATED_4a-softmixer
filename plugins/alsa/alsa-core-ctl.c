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
#include "alsa-bluez.h"

#include <pthread.h>
#include <sys/syscall.h>

typedef struct {
    SoftMixerT *mixer;
    sd_event_source* evtsrc;
    const char* uid;
    AlsaSndCtlT *sndcard;
    sd_event *sdLoop;
} SubscribeHandleT;

PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNumidElemId(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid) {
    char string[32];
    int error;
    int index;
    snd_ctl_elem_list_t *ctlList = NULL;
    snd_ctl_elem_id_t *elemId;

    snd_ctl_elem_list_alloca(&ctlList);

    if ((error = snd_ctl_elem_list(sndcard->ctl, ctlList)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s [%s] fail retrieve controls",
					 __func__, ALSA_CTL_UID(sndcard->ctl, string));
        goto OnErrorExit;
    }

    if ((error = snd_ctl_elem_list_alloc_space(ctlList, snd_ctl_elem_list_get_count(ctlList))) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s [%s] fail retrieve count",
					 __func__, ALSA_CTL_UID(sndcard->ctl, string));
        goto OnErrorExit;
    }

    // Fulup: do not understand why snd_ctl_elem_list should be call twice to get a valid ctlCount
    if ((error = snd_ctl_elem_list(sndcard->ctl, ctlList)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s [%s] fail retrieve controls",
					 __func__, ALSA_CTL_UID(sndcard->ctl, string));
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
        AFB_ApiNotice(mixer->api,
        		      "%s [%s] fail get numid=%i count",
					  __func__, ALSA_CTL_UID(sndcard->ctl, string), numid);
        goto OnErrorExit;
    }

    // clear ctl list and return elemid
    snd_ctl_elem_list_clear(ctlList);
    return elemId;

OnErrorExit:
    if (ctlList) snd_ctl_elem_list_clear(ctlList);
    return NULL;
}

PUBLIC snd_ctl_elem_id_t *AlsaCtlGetNameElemId(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName) {
    int error;
    int index;
    snd_ctl_elem_list_t *ctlList = NULL;
    snd_ctl_elem_id_t *elemId;

    snd_ctl_elem_list_alloca(&ctlList);

    if ((error = snd_ctl_elem_list(sndcard->ctl, ctlList)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s cardid='%s' cardname='%s' fail retrieve controls",
					 __func__, sndcard->cid.cardid, sndcard->cid.name);
        goto OnErrorExit;
    }

    if ((error = snd_ctl_elem_list_alloc_space(ctlList, snd_ctl_elem_list_get_count(ctlList))) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s cardid='%s' cardname='%s' fail retrieve count",
					 __func__, sndcard->cid.cardid, sndcard->cid.name);
        goto OnErrorExit;
    }

    // Fulup: do not understand why snd_ctl_elem_list should be call twice to get a valid ctlCount
    if ((error = snd_ctl_elem_list(sndcard->ctl, ctlList)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s cardid='%s' cardname='%s' fail retrieve controls",
					 __func__, sndcard->cid.cardid, sndcard->cid.name);
        goto OnErrorExit;
    }

    // loop on control to find the right one
    int ctlCount = snd_ctl_elem_list_get_used(ctlList);
    for (index = 0; index < ctlCount; index++) {

        if (strcasestr(snd_ctl_elem_list_get_name(ctlList, index), ctlName)) {
            snd_ctl_elem_id_malloc(&elemId);
            snd_ctl_elem_list_get_id(ctlList, index, elemId);
            break;
        }
    }

    if (index == ctlCount) {
        AFB_ApiNotice(mixer->api, "AlsaCtlGetNameElemId cardid='%s' cardname='%s' ctl not found name=%s", sndcard->cid.cardid, sndcard->cid.name, ctlName);
        goto OnErrorExit;
    }

    // clear ctl list and return elemid
    snd_ctl_elem_list_clear(ctlList);
    return elemId;

OnErrorExit:
    if (ctlList) snd_ctl_elem_list_clear(ctlList);
    return NULL;
}

PUBLIC snd_ctl_t *AlsaCtlOpenCtl(SoftMixerT *mixer, const char *cardid) {
    int error;
    snd_ctl_t *ctl;

    if (!cardid) goto OnErrorExit;

    if ((error = snd_ctl_open(&ctl, cardid, SND_CTL_READONLY)) < 0) {
        cardid = "Not Defined";
        goto OnErrorExit;
    }

    return ctl;

OnErrorExit:
    AFB_ApiError(mixer->api, "AlsaCtlOpenCtl: fail to find sndcard by id= %s", cardid);
    return NULL;
}


STATIC void CtlElemIdDisplay(SoftMixerT *mixer, snd_ctl_elem_info_t *elemInfo, snd_ctl_elem_value_t *elemData) {

    int numid = snd_ctl_elem_info_get_numid(elemInfo);
    int count = snd_ctl_elem_info_get_count(elemInfo);
    const char* name = snd_ctl_elem_info_get_name(elemInfo);
    snd_ctl_elem_type_t elemType = snd_ctl_elem_info_get_type(elemInfo);


    if (!elemData) {
        AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=unreadable", numid, name);
    } else
        for (int idx = 0; idx < count; idx++) {
            long valueL;

            switch (elemType) {
                case SND_CTL_ELEM_TYPE_BOOLEAN:
                    valueL = snd_ctl_elem_value_get_boolean(elemData, idx);
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_INTEGER:
                    valueL = snd_ctl_elem_value_get_integer(elemData, idx);
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_INTEGER64:
                    valueL = snd_ctl_elem_value_get_integer64(elemData, idx);
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_ENUMERATED:
                    valueL = snd_ctl_elem_value_get_enumerated(elemData, idx);
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_BYTES:
                    valueL = snd_ctl_elem_value_get_byte(elemData, idx);
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s value=%ld", numid, name, valueL);
                    break;
                case SND_CTL_ELEM_TYPE_IEC958:
                default:
                    AFB_ApiWarning(mixer->api, "CtlElemIdDisplay: numid=%d name=%s Unsupported type=%d", numid, name, elemType);
                    break;
            }
        }
}

PUBLIC int CtlElemIdGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, snd_ctl_elem_id_t *elemId, long *value) {
    int error;
    snd_ctl_elem_value_t *elemData;
    snd_ctl_elem_info_t *elemInfo;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(sndcard->ctl, elemInfo) < 0) goto OnErrorExit;
    if (!snd_ctl_elem_info_is_readable(elemInfo)) goto OnErrorExit;

    // as we have static rate/channel we should have only one boolean as value

    snd_ctl_elem_value_alloca(&elemData);
    snd_ctl_elem_value_set_id(elemData, elemId);
    error = snd_ctl_elem_read(sndcard->ctl, elemData);
    if (error) {
        elemData = NULL;
        goto OnErrorExit;
    }

    // value=1 when active and 0 when not active
    *value = (int) snd_ctl_elem_value_get_integer(elemData, 0);

    return 0;

OnErrorExit:
    CtlElemIdDisplay(mixer, elemInfo, elemData);
    return -1;
}

PUBLIC int CtlElemIdSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, snd_ctl_elem_id_t *elemId, long value) {
    snd_ctl_elem_value_t *elemData;
    snd_ctl_elem_info_t *elemInfo;
    const char* name;
    int error, numid;

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(sndcard->ctl, elemInfo) < 0) goto OnErrorExit;

    if (!snd_ctl_elem_info_is_writable(elemInfo)) goto OnErrorExit;

    int count = snd_ctl_elem_info_get_count(elemInfo);
    if (count == 0) goto OnErrorExit;

    snd_ctl_elem_value_alloca(&elemData);
    snd_ctl_elem_value_set_id(elemData, elemId);
    error = snd_ctl_elem_read(sndcard->ctl, elemData);
    if (error) goto OnErrorExit;

    for (int index = 0; index < count; index++) {
        snd_ctl_elem_value_set_integer(elemData, index, value);
    }

    error = snd_ctl_elem_write(sndcard->ctl, elemData);
    if (error) goto OnErrorExit;

    return 0;

OnErrorExit:
    numid = snd_ctl_elem_info_get_numid(elemInfo);
    name = snd_ctl_elem_info_get_name(elemInfo);
    AFB_ApiError(mixer->api, "CtlElemIdSetInt: numid=%d name=%s not writable", numid, name);
    return -1;
}

// Clone of AlsaLib snd_card_load2 static function

PUBLIC snd_ctl_card_info_t *AlsaCtlGetCardInfo(SoftMixerT *mixer, const char *cardid) {
    int error;
    snd_ctl_t *ctl;

    AFB_ApiNotice(mixer->api, "Looking for card '%s'", cardid);

    /* "bluealsa" is the name of the control external plugin
     * (https://www.alsa-project.org/alsa-doc/alsa-lib/ctl_external_plugins.html)
     */
    if (strstr(cardid, "bluealsa")) {
    	cardid="bluealsa";
    	alsa_bluez_init();
    }

    AFB_ApiNotice(mixer->api, "Opening card control '%s'", cardid);

    if ((error = snd_ctl_open(&ctl, cardid, SND_CTL_READONLY)) < 0) {
        cardid = "Not Defined";
        goto OnErrorExit;
    }

    snd_ctl_card_info_t *cardInfo = malloc(snd_ctl_card_info_sizeof());
    if ((error = snd_ctl_card_info(ctl, cardInfo)) < 0) {
        goto OnErrorExit;
    }
    return cardInfo;

OnErrorExit:
    AFB_ApiError(mixer->api, "AlsaCtlGetInfo: fail to find sndcard by id= %s", cardid);
    return NULL;
}

PUBLIC int AlsaCtlNumidSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid, long value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNumidElemId(mixer, sndcard, numid);
    if (!elemId) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to find numid=%d",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, numid);
        goto OnErrorExit;
    }

    int error = CtlElemIdSetLong(mixer, sndcard, elemId, value);
    if (error) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to set numid=%d value=%ld",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, numid, value);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlNumidGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, int numid, long* value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNumidElemId(mixer, sndcard, numid);
    if (!elemId) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to find numid=%d",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, numid);
        goto OnErrorExit;
    }

    int error = CtlElemIdGetLong(mixer, sndcard, elemId, value);
    if (error) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to get numid=%d value",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, numid);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlNameSetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName, long value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNameElemId(mixer, sndcard, ctlName);
    if (!elemId) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to find crlName=%s",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName);
        goto OnErrorExit;
    }

    int error = CtlElemIdSetLong(mixer, sndcard, elemId, value);
    if (error) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to set crlName=%s value=%ld",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName, value);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlNameGetLong(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName, long* value) {

    snd_ctl_elem_id_t *elemId = AlsaCtlGetNameElemId(mixer, sndcard, ctlName);
    if (!elemId) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to find crlName=%s",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName);
        goto OnErrorExit;
    }

    int error = CtlElemIdGetLong(mixer, sndcard, elemId, value);
    if (error) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to get crlName=%s value",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName);
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}

STATIC int AlsaCtlMakeControl(SoftMixerT *mixer, AlsaSndCtlT *sndcard, const char *ctlName, int ctlCount, int ctlMin, int ctlMax, int ctlStep) {
    snd_ctl_elem_type_t ctlType;
    snd_ctl_elem_info_t *elemInfo;
    int error;

    snd_ctl_elem_info_alloca(&elemInfo);
    if (ctlName) snd_ctl_elem_info_set_name(elemInfo, ctlName);
    snd_ctl_elem_info_set_interface(elemInfo, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_info(sndcard->ctl, elemInfo);

    // map volume to sndcard device+subdev=0
    snd_ctl_elem_info_set_device(elemInfo, sndcard->cid.device);
    snd_ctl_elem_info_set_subdevice(elemInfo, sndcard->cid.subdev);
    snd_ctl_elem_info_set_device(elemInfo, 0);
    snd_ctl_elem_info_set_subdevice(elemInfo, 0);

    // only two types implemented
    if (ctlMin == 0 && ctlMax == 1) ctlType = SND_CTL_ELEM_TYPE_BOOLEAN;
    else ctlType = SND_CTL_ELEM_TYPE_INTEGER;

    switch (ctlType) {
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            error = snd_ctl_add_boolean_elem_set(sndcard->ctl, elemInfo, 1, ctlCount);
            if (error) goto OnErrorExit;
            break;

        case SND_CTL_ELEM_TYPE_INTEGER:
            error = snd_ctl_add_integer_elem_set(sndcard->ctl, elemInfo, 1, ctlCount, ctlMin, ctlMax, ctlStep);
            if (error) goto OnErrorExit;
            break;

        default:
            AFB_ApiError(mixer->api,
            		     "%s: mixer=%s cardid=%s cardname=%s fail to create %s(control)",
						 __func__, mixer->uid, sndcard->cid.cardid, sndcard->cid.longname, ctlName);
            goto OnErrorExit;
    }

    // retrieve newly created control numid
    int numid = snd_ctl_elem_info_get_numid(elemInfo);
    return numid;

OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlCreateControl(SoftMixerT *mixer, AlsaSndCtlT *sndcard, char* ctlName, int ctlCount, int ctlMin, int ctlMax, int ctlStep, long value) {
    int numid = -1;

    // if control does not exist then create
    snd_ctl_elem_id_t *elemId = AlsaCtlGetNameElemId(mixer, sndcard, ctlName);
    if (elemId) {
        numid = snd_ctl_elem_id_get_numid(elemId);
    } else {
        // create or get numid control when already exist
        numid = AlsaCtlMakeControl(mixer, sndcard, ctlName, ctlCount, ctlMin, ctlMax, ctlStep);
        if (numid <= 0) {
            AFB_ApiError(mixer->api,
            		     "%s cardid=%s cardname=%s fail to create ctlName=%s",
						 __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName);
            goto OnErrorExit;
        }

        elemId = AlsaCtlGetNumidElemId(mixer, sndcard, numid);
    }

    int error = CtlElemIdSetLong(mixer, sndcard, elemId, value);
    if (error) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s fail to set ctlName=%s Numid=%d",
        		     __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName, numid);
        goto OnErrorExit;
    }

    AFB_ApiNotice(mixer->api,
    		      "%s cardid=%s cardname=%s ctl create name=%s numid=%d value=%ld",
    		      __func__, sndcard->cid.cardid, sndcard->cid.longname, ctlName, numid, value);
    return numid;
OnErrorExit:
    return -1;
}

STATIC int CtlSubscribeEventCB(sd_event_source* src, int fd, uint32_t revents, void* userData) {
    int error;
    SubscribeHandleT *sHandle = (SubscribeHandleT*) userData;
    AlsaSndCtlT *sndcard = sHandle->sndcard;
    SoftMixerT *mixer = sHandle->mixer;
    snd_ctl_event_t *eventId;
    snd_ctl_elem_id_t *elemId;
    long value;
    int index;

    if ((revents & EPOLLHUP) != 0) {
        AFB_ApiNotice(mixer->api, "%s hanghup [card:%s disconnected]", __func__, sHandle->uid);
        goto OnSuccessExit;
    }

    if ((revents & EPOLLIN) == 0) goto OnSuccessExit;

    // initialise event structure on stack
    snd_ctl_event_alloca(&eventId);
    snd_ctl_elem_id_alloca(&elemId);

    error = snd_ctl_read(sndcard->ctl, eventId);
    if (error < 0) goto OnErrorExit;

    // we only process sndctrl element
    if (snd_ctl_event_get_type(eventId) != SND_CTL_EVENT_ELEM) goto OnSuccessExit;

    // we only process value changed events
    unsigned int eventMask = snd_ctl_event_elem_get_mask(eventId);
    if (!(eventMask & SND_CTL_EVENT_MASK_VALUE)) goto OnSuccessExit;

    // extract element from event and get value    
    snd_ctl_event_elem_get_id(eventId, elemId);
    error = CtlElemIdGetLong(mixer, sHandle->sndcard, elemId, &value);
    if (error) goto OnErrorExit;
    
    // get numdid and name from elemId
    snd_ctl_elem_info_t *elemInfo;
    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    if (snd_ctl_elem_info(sndcard->ctl, elemInfo) < 0) goto OnErrorExit;
    int numid = snd_ctl_elem_info_get_numid(elemInfo);
    const char *name= snd_ctl_elem_info_get_name(elemInfo);

    for (index = 0; sndcard->registry[index]; index++) {
    	RegistryEntryPcmT * reg = sndcard->registry[index];
    	snd_pcm_t * pcm = reg->pcm->handle;
        if (reg->numid == numid) {
        	int ret;
            switch (reg->type) {
                case FONTEND_NUMID_RUN:
                    AlsaPcmCopyMuteSignal(mixer, reg->pcm, !value);
                    ret = snd_pcm_pause(pcm, (int) (!value));
                    AFB_ApiNotice(mixer->api, "%s:%s numid=%d name=%s active=%ld ret %d",
                    		      __func__, sHandle->uid, numid, name, value, ret);
                    if (ret < 0) {
                    	AFB_ApiNotice(mixer->api, "%s error: %s", __func__, snd_strerror(ret));
                    }

                    break;
                case FONTEND_NUMID_PAUSE:
                    AlsaPcmCopyMuteSignal(mixer, reg->pcm, value);
                    ret = snd_pcm_pause(reg->pcm->handle, (int) value);
                    AFB_ApiNotice(mixer->api, "%s:%s numid=%d name=%s pause=%ld ret %d",
                    		      __func__, sHandle->uid, numid, name, value, ret);
                    if (ret < 0) {
                    	AFB_ApiNotice(mixer->api, "%s error %s", __func__, snd_strerror(ret));
                    }
                    break;
                case FONTEND_NUMID_IGNORE:
                default:
                    AFB_ApiInfo(mixer->api,
                    			"%s:%s numid=%d name=%s ignored=%ld",
								__func__, sHandle->uid, numid, name, value);
            }
            break;
        }
    }
    if (index == sndcard->rcount) {
        AFB_ApiNotice(mixer->api, "%s:%s numid=%d (unknown)", __func__, sHandle->uid, numid);
    }

OnSuccessExit:
    return 0;

OnErrorExit:
    AFB_ApiInfo(mixer->api, "%s: ignored unsupported event", __func__);
    return 0;
}

PUBLIC snd_ctl_t* AlsaCrlFromPcm(SoftMixerT *mixer, snd_pcm_t *pcm) {
    char buffer[32];
    int error;
    snd_ctl_t *ctl;
    snd_pcm_info_t *pcmInfo;

    snd_pcm_info_alloca(&pcmInfo);
    if ((error = snd_pcm_info(pcm, pcmInfo)) < 0) goto OnErrorExit;

    int pcmCard = snd_pcm_info_get_card(pcmInfo);
    snprintf(buffer, sizeof (buffer), "hw:%i", pcmCard);
    if ((error = snd_ctl_open(&ctl, buffer, SND_CTL_READONLY)) < 0) goto OnErrorExit;

    return ctl;

OnErrorExit:
    return NULL;
}

PUBLIC int AlsaCtlSubscribe(SoftMixerT *mixer, const char *uid, AlsaSndCtlT *sndcard) {
    int error;
    char string [32];
    struct pollfd pfds;
    SubscribeHandleT *handle = malloc(sizeof (SubscribeHandleT));

    handle->mixer = mixer;
    handle->sndcard = sndcard;
    handle->uid = uid;

    // subscribe for sndctl events attached to cardid
    if ((error = snd_ctl_subscribe_events(handle->sndcard->ctl, 1)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: fail sndcard=%s to subscribe events",
					 __func__, ALSA_CTL_UID(handle->sndcard->ctl, string));
        goto OnErrorExit;
    }

    // get pollfd attach to this sound board
    int count = snd_ctl_poll_descriptors(handle->sndcard->ctl, &pfds, 1);
    if (count != 1) {
        AFB_ApiError(mixer->api,
        		     "%s: fail sndcard=%s get poll descriptors",
					 __func__, ALSA_CTL_UID(handle->sndcard->ctl, string));
        goto OnErrorExit;
    }

    // Registry sound event to binder main loop
    if ((error = sd_event_add_io(mixer->sdLoop, &handle->evtsrc, pfds.fd, EPOLLIN, CtlSubscribeEventCB, handle)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: Fail sndcard=%s adding mainloop",
					 __func__, ALSA_CTL_UID(handle->sndcard->ctl, string));
        goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return -1;
}

PUBLIC int AlsaCtlRegister(SoftMixerT *mixer, AlsaSndCtlT *sndcard, AlsaPcmCtlT *pcmdev, RegistryNumidT type, int numid) {
    int index;

    AFB_ApiInfo(mixer->api,"%s: %d!\n", __func__, numid);

    for (index = 0; index < sndcard->rcount; index++) {
        if (!sndcard->registry[index]) break;
    }

    if (index == sndcard->rcount) {
        AFB_ApiError(mixer->api,
        		     "%s cardid=%s cardname=%s to many audio stream max=%ld",
					 __func__, sndcard->cid.cardid, sndcard->cid.longname, sndcard->rcount);
        goto OnErrorExit;
    }

    // If 1st registration then register to card event
    if (index == 0) {
        AlsaCtlSubscribe(mixer, sndcard->cid.cardid, sndcard);
    }

    // store PCM in order to pause/resume depending on event
    RegistryEntryPcmT *entry = calloc(1, sizeof (RegistryEntryPcmT));
    sndcard->registry[index] = entry;
    entry->pcm = pcmdev;
    entry->numid = numid;
    entry->type = type;

    return 0;

OnErrorExit:
    return -1;
}
