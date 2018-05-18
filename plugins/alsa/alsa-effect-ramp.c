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
    const char *uid;
    long current; 
    long target; 
    int numid;
    snd_ctl_t *ctlDev;
    AlsaVolRampT *params;
    sd_event_source *evtsrc; 
    CtlSourceT *source;
    sd_event *sdLoop;
} VolRampHandleT;

STATIC int VolRampTimerCB(sd_event_source* source, uint64_t timer, void* handle) {
    VolRampHandleT *rampHandle = (VolRampHandleT*) handle;
    int error;
    uint64_t usec;

    // RampDown
    if (rampHandle->current > rampHandle->target) {
        rampHandle->current = rampHandle->current - rampHandle->params->stepDown;
        if (rampHandle->current < rampHandle->target) rampHandle->current = rampHandle->target;
    }

    // RampUp
    if (rampHandle->current < rampHandle->target) {
        rampHandle->current = rampHandle->current + rampHandle->params->stepUp;
        if (rampHandle->current > rampHandle->target) rampHandle->current = rampHandle->target;
    }
    
    error = AlsaCtlNumidSetLong(rampHandle->source, rampHandle->ctlDev, rampHandle->numid, rampHandle->current);
    if (error) goto OnErrorExit;

    // we reach target stop volram event
    if (rampHandle->current == rampHandle->target) {
        sd_event_source_unref(rampHandle->evtsrc);
        snd_ctl_close (rampHandle->ctlDev);
        free (rampHandle);
    }
    else {
        // otherwise validate timer for a new run
        sd_event_now(rampHandle->sdLoop, CLOCK_MONOTONIC, &usec);
        sd_event_source_set_enabled(rampHandle->evtsrc, SD_EVENT_ONESHOT);
        error = sd_event_source_set_time(rampHandle->evtsrc, usec + rampHandle->params->delay);
    }

    return 0;

OnErrorExit:
    AFB_ApiWarning(rampHandle->source->api, "VolRampTimerCB stream=%s numid=%d value=%ld", rampHandle->uid, rampHandle->numid, rampHandle->current);
    sd_event_source_unref(source); // abandon volRamp
    return -1;
}

PUBLIC int AlsaVolRampApply(CtlSourceT *source, AlsaSndLoopT *frontend, AlsaLoopStreamT *stream, AlsaVolRampT *ramp, json_object *volumeJ) {
    long curvol, newvol;
    const char*volString;
    int error;
    uint64_t usec;
    
    snd_ctl_t *ctlDev =AlsaCtlOpenCtl(source, frontend->cardid);
    if (!ctlDev) goto OnErrorExit;
            
    error = AlsaCtlNumidGetLong(source, ctlDev, stream->volume, &curvol);
    if (error) {
        AFB_ApiError(source->api,"AlsaVolRampApply:%s(stream) Fail to get volume numid=%d", stream->uid, stream->volume);
        goto OnErrorExit;
    }

    switch (json_object_get_type(volumeJ)) {
        case json_type_string:
            volString = json_object_get_string(volumeJ);
            switch (volString[0]) {
                case '+':
                    sscanf(&volString[1], "%ld", &newvol);
                    newvol = curvol + newvol;
                    break;

                case '-':
                    sscanf(&volString[1], "%ld", &newvol);
                    newvol = curvol - newvol;
                    break;
                default:
                    AFB_ApiError(source->api,"AlsaVolRampApply:%s(stream) relative volume should start by '+|-' value=%s", stream->uid, json_object_get_string(volumeJ));
                    goto OnErrorExit;
            }
            break;
        case json_type_int:
            newvol = json_object_get_int(volumeJ);
            break;
        default:
            AFB_ApiError(source->api,"AlsaVolRampApply:%s(stream) volume should be string or integer value=%s", stream->uid, json_object_get_string(volumeJ));
            goto OnErrorExit;

    }

    VolRampHandleT *rampHandle = calloc(1, sizeof (VolRampHandleT));
    rampHandle->uid= stream->uid;
    rampHandle->numid= stream->volume;
    rampHandle->ctlDev= ctlDev;
    rampHandle->source = source;
    rampHandle->params = ramp;
    rampHandle->target = newvol;
    rampHandle->current= curvol;
    rampHandle->sdLoop= stream->copy.sdLoop;
    
    // set a timer with ~250us accuracy
    sd_event_now(rampHandle->sdLoop, CLOCK_MONOTONIC, &usec);
    (void)sd_event_add_time(rampHandle->sdLoop, &rampHandle->evtsrc, CLOCK_MONOTONIC, usec+100, 250, VolRampTimerCB, rampHandle);

    return 0;

OnErrorExit:
    return -1;
}
