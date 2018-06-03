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

#include <pthread.h>
#include <sys/syscall.h>

#include "alsa-softmixer.h"

typedef struct {
    const char *uid;
    long current;
    long target;
    int numid;
    AlsaSndCtlT *sndcard;
    AlsaVolRampT *ramp;
    sd_event_source *evtsrc;
    SoftMixerT *mixer;
    sd_event *sdLoop;
} VolRampHandleT;

STATIC int VolRampTimerCB(sd_event_source* source, uint64_t timer, void* handle) {
    VolRampHandleT *rHandle = (VolRampHandleT*)handle;
    int error;
    uint64_t usec;

    // RampDown
    if (rHandle->current > rHandle->target) {
        rHandle->current = rHandle->current - rHandle->ramp->stepDown;
        if (rHandle->current < rHandle->target) rHandle->current = rHandle->target;
    }

    // RampUp
    if (rHandle->current < rHandle->target) {
        rHandle->current = rHandle->current + rHandle->ramp->stepUp;
        if (rHandle->current > rHandle->target) rHandle->current = rHandle->target;
    }

    error = AlsaCtlNumidSetLong(rHandle->mixer, rHandle->sndcard, rHandle->numid, rHandle->current);
    if (error) goto OnErrorExit;

    // we reach target stop volram event
    if (rHandle->current == rHandle->target) {
        sd_event_source_unref(rHandle->evtsrc);
        free(rHandle);
    } else {
        // otherwise validate timer for a new run
        sd_event_now(rHandle->sdLoop, CLOCK_MONOTONIC, &usec);
        sd_event_source_set_enabled(rHandle->evtsrc, SD_EVENT_ONESHOT);
        error = sd_event_source_set_time(rHandle->evtsrc, usec + rHandle->ramp->delay);
    }

    return 0;

OnErrorExit:
    AFB_ApiWarning(rHandle->mixer->api, "VolRampTimerCB stream=%s numid=%d value=%ld", rHandle->uid, rHandle->numid, rHandle->current);
    sd_event_source_unref(source); // abandon volRamp
    return -1;
}

PUBLIC int AlsaVolRampApply(SoftMixerT *mixer, AlsaSndCtlT *sndcard, AlsaStreamAudioT *stream, json_object *rampJ) {
    long curvol, newvol;
    const char *uid, *volS;
    json_object *volJ;
    int error, index;
    uint64_t usec;

    error = wrap_json_unpack(rampJ, "{ss so !}"
            , "uid", &uid
            , "vol", &volJ
            );
    if (error) {
        AFB_ApiError(mixer->api, "AlsaVolRampApply:mixer=%s stream=%s invalid-json should {uid:ramp, vol:[+,-,=]value} ramp=%s", mixer->uid, stream->uid, json_object_get_string(rampJ));
        goto OnErrorExit;
    }

    switch (json_object_get_type(volJ)) {
        int count;
        
        case json_type_string:
            volS = json_object_get_string(volJ);

            switch (volS[0]) {
                case '+':
                    count= sscanf(&volS[1], "%ld", &newvol);
                    newvol = curvol + newvol;
                    break;

                case '-':
                    count= sscanf(&volS[1], "%ld", &newvol);
                    newvol = curvol - newvol;
                    break;

                case '=':
                    count= sscanf(&volS[1], "%ld", &newvol);
                    break;

                default:
                    // hope for int as a string and force it as relative
                    sscanf(&volS[0], "%ld", &newvol);
                    if (newvol < 0) newvol = curvol - newvol;
                    else newvol = curvol + newvol;
            }
            
            if (count != 1) {
                AFB_ApiError(mixer->api, "AlsaVolRampApply:mixer=%s stream=%s invalid-numeric expect {uid:%s, vol:[+,-,=]value} get vol:%s", mixer->uid, stream->uid, uid, json_object_get_string(volJ));
                goto OnErrorExit;                
            }
            break;
        case json_type_int:
            newvol = json_object_get_int(volJ);
            break;

        default:
            AFB_ApiError(mixer->api, "AlsaVolRampApply:mixer=%s stream=%s invalid-type expect {uid:%s, vol:[+,-,=]value} get vol:%s", mixer->uid, stream->uid, uid, json_object_get_string(volJ));
            goto OnErrorExit;

    }

    error = AlsaCtlNumidGetLong(mixer, sndcard, stream->volume, &curvol);
    if (error) {
        AFB_ApiError(mixer->api, "AlsaVolRampApply:mixer=%s stream=%s ramp=%s Fail to get volume from numid=%d", mixer->uid, stream->uid, uid, stream->volume);
        goto OnErrorExit;
    }
    
    // search for ramp uid in mixer
    for (index=0; index<= mixer->max.ramps; index++) {
        if (!strcasecmp(mixer->ramps[index]->uid, uid)) {
            break;
        }
    }
    
    if (index == mixer->max.ramps) {
        AFB_ApiError(mixer->api, "AlsaVolRampApply:mixer=%s stream=%s ramp=%s does not exit", mixer->uid, stream->uid, uid);
        goto OnErrorExit;        
    }

    VolRampHandleT *rHandle = calloc(1, sizeof (VolRampHandleT));
    rHandle->uid = stream->uid;
    rHandle->numid = stream->volume;
    rHandle->sndcard = sndcard;
    rHandle->mixer = mixer;
    rHandle->ramp = mixer->ramps[index];
    rHandle->target = newvol;
    rHandle->current = curvol;
    rHandle->sdLoop = mixer->sdLoop;

    // set a timer with ~250us accuracy
    sd_event_now(rHandle->sdLoop, CLOCK_MONOTONIC, &usec);
    (void) sd_event_add_time(rHandle->sdLoop, &rHandle->evtsrc, CLOCK_MONOTONIC, usec + 100, 250, VolRampTimerCB, rHandle);

    return 0;

OnErrorExit:
    return -1;
}
