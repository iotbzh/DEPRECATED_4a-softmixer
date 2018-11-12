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

#include "alsa-softmixer.h"
#include <avirt/avirt.h>
#include <string.h>

#define UID_AVIRT_LOOP "AVIRT-Loopback"

#define REGNUMID_0  51
#define REGNUMID_1  57
#define REGNUMID_2  63
#define REGNUMID_3  69
#define REGNUMID_4  75
#define REGNUMID_5  81
#define REGNUMID_6  87
#define REGNUMID_7  93
#define REGNUMID_8  99
#define REGNUMID_9  105
#define REGNUMID_10 111
#define REGNUMID_11 117
#define REGNUMID_12 123
#define REGNUMID_13 129
#define REGNUMID_14 135
#define REGNUMID_15 141

struct RegistryNumidMap {
    int index;
    int numid;
} numidmap[] = {
    { 0,  REGNUMID_0  },
    { 1,  REGNUMID_1  },
    { 2,  REGNUMID_2  },
    { 3,  REGNUMID_3  },
    { 4,  REGNUMID_4  },
    { 5,  REGNUMID_5  },
    { 6,  REGNUMID_6  },
    { 7,  REGNUMID_7  },
    { 8,  REGNUMID_8  },
    { 9,  REGNUMID_9  },
    { 10, REGNUMID_10 },
    { 11, REGNUMID_11 },
    { 12, REGNUMID_12 },
    { 13, REGNUMID_13 },
    { 14, REGNUMID_14 },
    { 15, REGNUMID_15 },
};

PUBLIC AlsaLoopSubdevT *ApiLoopFindSubdev(SoftMixerT *mixer, const char *streamUid, const char *targetUid, AlsaSndLoopT **loop) {

    // Either allocate a free loop subdev or search for a specific targetUid when specified
    if (targetUid) {
        for (int idx = 0; mixer->loops[idx]; idx++) {
            for (int jdx = 0; jdx < mixer->loops[idx]->scount; jdx++) {
                if (mixer->loops[idx]->subdevs[jdx]->uid && !strcasecmp(mixer->loops[idx]->subdevs[jdx]->uid, targetUid)) {
                    *loop = mixer->loops[idx];
                    return mixer->loops[idx]->subdevs[jdx];
                }
            }
        }
    } else {
        for (int idx = 0; mixer->loops[idx]; idx++) {
            for (int jdx = 0; mixer->loops[idx]->subdevs[jdx]; jdx++) {
                if (!mixer->loops[idx]->subdevs[jdx]->uid) {
                    mixer->loops[idx]->subdevs[jdx]->uid = streamUid;
                    *loop = mixer->loops[idx];
                    return mixer->loops[idx]->subdevs[jdx];
                }
            }
        }
    }
    return NULL;
}

STATIC int CheckOneSubdev(SoftMixerT *mixer, AlsaSndLoopT *loop, AlsaLoopSubdevT *subdev) {
    // create loop subdev entry point with cardidx+device+subdev in order to open subdev and not sndcard
    AlsaDevInfoT loopSubdev;
    loopSubdev.devpath=NULL;
    loopSubdev.cardid=NULL;
    loopSubdev.pcmplug_params = NULL;
    loopSubdev.cardidx = loop->sndcard->cid.cardidx;
    loopSubdev.device = loop->capture;
    loopSubdev.subdev = subdev->index;

    // assert we may open this loopback subdev in capture mode
    AlsaPcmCtlT *pcmInfo = AlsaByPathOpenPcm(mixer, &loopSubdev, SND_PCM_STREAM_CAPTURE);
    if (!pcmInfo)
      goto OnErrorExit;

    // free PCM as we only open loop to assert it's a valid capture device
    snd_pcm_close(pcmInfo->handle);
    free(pcmInfo);

    return 0;

OnErrorExit:
    AFB_ApiError(mixer->api, "%s: Failed", __func__);

    return -1;
}

STATIC AlsaLoopSubdevT *ProcessOneAvirtSubdev(SoftMixerT *mixer, AlsaSndLoopT *loop, int index) {
    AFB_ApiInfo(mixer->api, "%s: %d", __func__, index);

    AlsaLoopSubdevT *subdev = calloc(1, sizeof (AlsaPcmCtlT));
    subdev->uid = NULL;
    subdev->index = index;
    subdev->numid = numidmap[index].index;

    if (CheckOneSubdev(mixer, loop, subdev) < 0)
      return NULL;
    
    return subdev;
}

STATIC AlsaLoopSubdevT *ProcessOneSubdev(SoftMixerT *mixer, AlsaSndLoopT *loop, json_object *subdevJ) {
    AlsaLoopSubdevT *subdev = calloc(1, sizeof (AlsaPcmCtlT));

    int error = wrap_json_unpack(subdevJ, "{s?s, si,si !}"
            , "uid", &subdev->uid
            , "subdev", &subdev->index
            , "numid", &subdev->numid
            );
    if (error) {
        AFB_ApiError(mixer->api, "ProcessOneSubdev: loop=%s missing (uid|subdev|numid) error=%s json=%s", loop->uid, wrap_json_get_error_string(error),json_object_get_string(subdevJ));
        goto OnErrorExit;
    }

    // subdev with no UID are dynamically attached
    if (subdev->uid) subdev->uid = strdup(subdev->uid);

    if (CheckOneSubdev(mixer, loop, subdev) < 0)
      goto OnErrorExit;
    
    return subdev;

OnErrorExit:
    return NULL;
}

STATIC int AttachOneAvirtLoop(SoftMixerT *mixer, json_object *streamJ) {
    char *uid, *zone_uid;
    int error;
    AlsaSndZoneT *zone;

    uid = alloca(32);

    error = wrap_json_unpack(streamJ, "{ss,s?s,s?s,ss,s?s,s?i,s?b,s?o,s?s !}"
            , "uid", &uid
            , "verb", NULL
            , "info", NULL
            , "zone", &zone_uid
            , "source", NULL
            , "volume", NULL
            , "mute", NULL
            , "params", NULL
            , "ramp", NULL
            );

    if (error)
    {
        AFB_ApiNotice(mixer->api,
                       "%s: hal=%s missing 'uid|[info]|zone|source||[volume]|[mute]|[params]' error=%s stream=%s",
                       __func__, uid, wrap_json_get_error_string(error), json_object_get_string(streamJ));
        goto OnErrorExit;
    }

    if (mixer->zones[0]) {
      zone = ApiZoneGetByUid(mixer, zone_uid);
    } else {
      AFB_ApiError(mixer->api, "%s: No zones defined!", __func__);
      goto OnErrorExit;
    }

    error = snd_avirt_stream_new(uid, zone->ccount,
                                 SND_PCM_STREAM_PLAYBACK, "ap_loopback");
    if (error < 0)
    {
      AFB_ApiError(mixer->api,
      "%s: mixer=%s stream=%s could not create AVIRT stream [errno=%d]",
      __func__, mixer->uid, uid, error);
      return error;
    }

    return 0;

OnErrorExit:
    AFB_ApiError(mixer->api, "%s fail", __func__);
    return 0;
}

STATIC AlsaSndLoopT *AttachOneLoop(SoftMixerT *mixer, const char *uid, json_object *argsJ, json_object *streamsJ) {
    AlsaSndLoopT *loop = calloc(1, sizeof (AlsaSndLoopT));
    json_object *subdevsJ = NULL, *devicesJ = NULL;
    int error, index;

    AFB_ApiNotice(mixer->api, "%s", __func__);

    loop->sndcard = (AlsaSndCtlT*) calloc(1, sizeof (AlsaSndCtlT));
    error = wrap_json_unpack(argsJ, "{ss,s?s,s?s,s?o,s?o !}"
            , "uid", &loop->uid
            , "path", &loop->sndcard->cid.devpath
            , "cardid", &loop->sndcard->cid.cardid
            , "devices", &devicesJ
            , "subdevs", &subdevsJ
            );

    if (loop->uid) {
        if (!strcmp(loop->uid, UID_AVIRT_LOOP))
            loop->avirt = true;
        else
            loop->avirt = false;
    }

    if (error || !loop->uid || (!loop->avirt && !subdevsJ) || (!loop->sndcard->cid.devpath && !loop->sndcard->cid.cardid)) {
        AFB_ApiNotice(mixer->api, "%s mixer=%s hal=%s missing 'uid|path|cardid|devices|subdevs' error=%s args=%s",
        		__func__, mixer->uid, uid, wrap_json_get_error_string(error),json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    if (!loop->avirt) {
        // try to open sound card control interface
        loop->sndcard->ctl = AlsaByPathOpenCtl(mixer, loop->uid, loop->sndcard);
        if (!loop->sndcard->ctl) {
            AFB_ApiError(mixer->api, "%s mixer=%s hal=%s Fail open sndcard loop=%s devpath=%s cardid=%s (please check 'modprobe snd_aloop')",
                __func__, mixer->uid, uid, loop->uid, loop->sndcard->cid.devpath, loop->sndcard->cid.cardid);
            goto OnErrorExit;
      }

      // Default devices is payback=0 capture=1
      if (!devicesJ) {
          loop->playback = 0;
          loop->capture = 1;
      } else {
          error = wrap_json_unpack(devicesJ, "{si,si !}", "capture", &loop->capture, "playback", &loop->playback);
          if (error) {
              AFB_ApiNotice(mixer->api, "%s mixer=%s hal=%s Loop=%s missing 'capture|playback' error=%s devices=%s",
                  __func__, mixer->uid, uid, loop->uid, wrap_json_get_error_string(error),json_object_get_string(devicesJ));
              goto OnErrorExit;
          }
      }

      switch (json_object_get_type(subdevsJ)) {
          case json_type_object:
              loop->scount = 1;
              loop->subdevs = calloc(2, sizeof (void*));
              loop->subdevs[0] = ProcessOneSubdev(mixer, loop, subdevsJ);
              if (!loop->subdevs[0]) goto OnErrorExit;
              break;
          case json_type_array:
              loop->scount = (int) json_object_array_length(subdevsJ);
              loop->subdevs = calloc(loop->scount + 1, sizeof (void*));
              for (int idx = 0; idx < loop->scount; idx++) {
                  json_object *subdevJ = json_object_array_get_idx(subdevsJ, idx);
                  loop->subdevs[idx] = ProcessOneSubdev(mixer, loop, subdevJ);
                  if (!loop->subdevs[idx]) goto OnErrorExit;
              }
              break;
          default:
              AFB_ApiError(mixer->api, "%s mixer=%s hal=%s Loop=%s invalid subdevs= %s",
                  __func__, mixer->uid, uid, loop->uid, json_object_get_string(subdevsJ));
              goto OnErrorExit;
      }
    } else { // loop->avirt == true
        for (index = 0; index < mixer->max.streams; index++) {
            if (!mixer->streams[index]) break;
        }

        if (index == mixer->max.streams)
            goto OnErrorExit;

        // Create AVIRT streams
        switch (json_object_get_type(streamsJ)) {
            case json_type_object:
                loop->scount = 1;
                loop->subdevs = calloc(2, sizeof (void*));
                AttachOneAvirtLoop(mixer, streamsJ);
                loop->subdevs[0] = ProcessOneAvirtSubdev(mixer, loop, 0);
                break;

            case json_type_array:
                loop->scount = json_object_array_length(streamsJ);
                if (loop->scount > (mixer->max.streams - index))
                    goto OnErrorExit;
                loop->subdevs = calloc(loop->scount + 1, sizeof (void*));
                for (int idx = 0; idx < loop->scount; idx++) {
                    json_object *streamJ = json_object_array_get_idx(streamsJ, idx);
                    AttachOneAvirtLoop(mixer, streamJ);
                    loop->subdevs[idx] = ProcessOneAvirtSubdev(mixer, loop, idx);
                }
                break;
            default:
                goto OnErrorExit;
        }

        snd_avirt_card_seal();

        // try to open sound card control interface
        loop->sndcard->ctl = AlsaByPathOpenCtl(mixer, loop->uid, loop->sndcard);
        if (!loop->sndcard->ctl) {
            AFB_ApiError(mixer->api, "%s mixer=%s hal=%s Fail open sndcard loop=%s devpath=%s cardid=%s (please check 'modprobe snd_aloop')",
                __func__, mixer->uid, uid, loop->uid, loop->sndcard->cid.devpath, loop->sndcard->cid.cardid);
            goto OnErrorExit;
        }
    }

    // we may have to register up to 3 control per subdevice (vol, pause, actif)
    loop->sndcard->registry = calloc(loop->scount * SMIXER_SUBDS_CTLS + 1, sizeof (RegistryEntryPcmT));
    loop->sndcard->rcount = loop->scount*SMIXER_SUBDS_CTLS;

    return loop;

OnErrorExit:
    return NULL;
}

PUBLIC int ApiLoopAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ, json_object *streamsJ) {

    int index;
    for (index = 0; index < mixer->max.loops; index++) {
        if (!mixer->loops[index]) break;
    }

    if (index == mixer->max.loops) {
        AFB_IfReqFailF(mixer, request, "too-small", "mixer=%s hal=%s max loop=%d", mixer->uid, uid, mixer->max.loops);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
            size_t count;

        case json_type_object:
            mixer->loops[index] = AttachOneLoop(mixer, uid, argsJ, streamsJ);
            if (!mixer->loops[index]) {
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.loops - index)) {
                AFB_IfReqFailF(mixer, request, "too-small", "mixer=%s hal=%s max loop=%d", mixer->uid, uid, mixer->max.loops);
                goto OnErrorExit;

            }

            for (int idx = 0; idx < count; idx++) {
                json_object *loopJ = json_object_array_get_idx(argsJ, idx);
                mixer->loops[index + idx] = AttachOneLoop(mixer, uid, loopJ, streamsJ);
                if (!mixer->loops[index + idx]) {
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_IfReqFailF(mixer, request, "bad-loop", "mixer=%s hal=%s loops invalid argsJ= %s", mixer->uid, uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}
