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
#include <string.h>

// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;

STATIC int ProcessOneSubdev(CtlSourceT *source, AlsaSndLoopT *loop, json_object *subdevJ, AlsaPcmInfoT *subdev) {

    int error = wrap_json_unpack(subdevJ, "{si,si !}", "subdev", &subdev->subdev, "numid", &subdev->numid);
    if (error) {
        AFB_ApiError(source->api, "ProcessOneSubdev: loop=%s missing (uid|subdev|numid) json=%s", loop->uid, json_object_get_string(subdevJ));
        goto OnErrorExit;
    }

    // create a fake uid and complete subdev info from loop handle
    char subuid[30];
    snprintf(subuid,sizeof(subuid),"loop:/%i/%i", subdev->subdev,subdev->numid);
    subdev->uid = strdup(subuid);
    subdev->device  = loop->capture; // Fulup: with alsaloop softmixer only use capture device (playback is used by applications)
    subdev->devpath = loop->devpath;
    subdev->cardid  = NULL;  // force AlsaByPathDevId to rebuild a new one for each subdev
    subdev->cardidx = loop->cardidx;
    
    // check if card exist
    error = AlsaByPathDevid(source, subdev);
    if (error) {
        AFB_ApiError(source->api, "ProcessOneSubdev: loop=%s fail to open subdev=%s", loop->uid, json_object_get_string(subdevJ));
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

STATIC int ProcessOneLoop(CtlSourceT *source, json_object *loopJ, AlsaSndLoopT *loop) {
    json_object *subdevsJ=NULL, *devicesJ=NULL;
    int error;

    error = wrap_json_unpack(loopJ, "{ss,s?s,s?s,s?i,s?o,so}", "uid",&loop->uid, "devpath",&loop->devpath, "cardid",&loop->cardid
              , "cardidx",&loop->cardidx, "devices",&devicesJ, "subdevs",&subdevsJ);
    if (error || !loop->uid || !subdevsJ || (!loop->devpath && !loop->cardid && loop->cardidx)) {
        AFB_ApiNotice(source->api, "ProcessOneLoop missing 'uid|devpath|cardid|cardidx|devices|subdevs' loop=%s", json_object_get_string(loopJ));
        goto OnErrorExit;
    }
    
    // make sure useful information will not be removed
    loop->uid=strdup(loop->uid);
    if (loop->cardid) loop->cardid=strdup(loop->cardid);
    if (loop->devpath) loop->cardid=strdup(loop->devpath);
    
    // Default devices is payback=0 capture=1
    if (!devicesJ) {
        loop->playback=0;
        loop->capture=1;       
    } else {
        error = wrap_json_unpack(devicesJ, "{si,si}", "capture",&loop->capture, "playback", &loop->playback);
        if (error) {
            AFB_ApiNotice(source->api, "ProcessOneLoop=%s missing 'capture|playback' devices=%s", loop->uid, json_object_get_string(devicesJ));
            goto OnErrorExit;
        }
    }

    switch (json_object_get_type(subdevsJ)) {
        case json_type_object:
            loop->scount = 1;
            loop->subdevs = calloc(loop->scount+1, sizeof (AlsaPcmInfoT));
            error = ProcessOneSubdev(source, loop, subdevsJ, &loop->subdevs[0]);
            if (error) goto OnErrorExit;
            break;
        case json_type_array:
            loop->scount = (int)json_object_array_length(subdevsJ);
            loop->subdevs = calloc(loop->scount+1, sizeof (AlsaPcmInfoT));
            for (int idx = 0; idx < loop->scount; idx++) {
                json_object *subdevJ = json_object_array_get_idx(subdevsJ, idx);
                error = ProcessOneSubdev(source, loop, subdevJ, &loop->subdevs[idx]);
                if (error) goto OnErrorExit;
            }
            break;
        default:
            AFB_ApiError(source->api, "L2C:ProcessOneLoop=%s invalid subdevs= %s",  loop->uid, json_object_get_string(subdevsJ));
            goto OnErrorExit;
    }
      
    return 0;

OnErrorExit:
    return -1;
}

CTLP_LUA2C(snd_loops, source, argsJ, responseJ) {
    int error;
    AlsaSndLoopT *sndLoop = calloc (1, sizeof(AlsaSndLoopT));

    if (json_object_get_type(argsJ) != json_type_object) {
        AFB_ApiError(source->api, "L2C:sndloops: invalid object type= %s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    error = ProcessOneLoop(source, argsJ, sndLoop);
    if (error) {
        AFB_ApiError(source->api, "L2C:sndloops: invalid object= %s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // register routed into global softmixer handle
    Softmixer->loopCtl = sndLoop;


    return 0;

OnErrorExit:
    return -1;
}