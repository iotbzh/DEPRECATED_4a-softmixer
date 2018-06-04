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
#include <pthread.h>


extern Lua2cWrapperT Lua2cWrap;

static void MixerRemoveVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_request_get_vcbdata(request);
    int error;

    for (int idx = 0; mixer->streams[idx]->uid; idx++) {
        AlsaStreamAudioT *stream = mixer->streams[idx];

        AFB_ApiNotice(mixer->api, "cleaning mixer=%s stream=%s", mixer->uid, stream->uid);

        error = pthread_cancel(stream->copy->thread);
        if (error) {
            AFB_ReqFailF(request, "internal-error", "Fail to kill audio-stream threads mixer=%s", mixer->uid);
            goto OnErrorExit;
        }

        error = afb_dynapi_sub_verb(mixer->api, stream->uid);
        if (error) {
            AFB_ReqFailF(request, "internal-error", "Mixer=%s fail to remove verb=%s error=%s", mixer->uid, stream->uid, strerror(error));
            goto OnErrorExit;
        }

        // free audio-stream dynamic structures
        snd_pcm_close(mixer->streams[idx]->copy->pcmIn);
        snd_pcm_close(mixer->streams[idx]->copy->pcmOut);
        if (stream->copy->evtsrc) sd_event_source_unref(stream->copy->evtsrc);
        if (stream->copy->sdLoop) sd_event_unref(stream->copy->sdLoop);
    }

    //    // (Fulup to be Done) registry is attached to source
    //    if (mixer->sources) ApiSourcFree (mixer);
    //    if (mixer->sinks) ApiSinkFree (mixer);
    //    if (mixer->loops) ApiLoopFree (mixer);
    //    if (mixer->ramps) ApiRampFree (mixer);
    //    if (mixer->zones) ApiZoneFree (mixer);

    // finally free mixer handle
    free(mixer);
    AFB_ReqSucess(request, NULL, "Fulup: delete might not clean everything properly");

    return;

OnErrorExit:
    AFB_ReqFail(request, "internal-error", "fail to delete mixer");
}

STATIC void MixerInfoVerb(AFB_ReqT request) {

    SoftMixerT *mixer = (SoftMixerT*) afb_request_get_vcbdata(request);
    json_object *argsJ = afb_request_json(request);
    int error, stream = 0, quiet = 0, backend = 0, source = 0, zones = 0;

    if (json_object_get_type(argsJ) == json_type_null) {
        stream = 1;
    } else {
        error = wrap_json_unpack(argsJ, "{s?b,s?b,s?b,s?b,s?b !}"
                , "quiet", &quiet
                , "stream", &stream
                , "backend", &backend
                , "source", &source
                , "zones", &zones
                );
        if (error) {
            AFB_ReqFailF(request, "invalid-syntax", "list missing 'quiet|stream|backend|source' argsJ=%s", json_object_get_string(argsJ));
            goto OnErrorExit;
        }
    }
    json_object *responseJ = json_object_new_object();

    if (stream) {
        json_object *streamsJ = json_object_new_array();
        json_object *valueJ;

        AlsaStreamAudioT **streams = mixer->streams;
        for (int idx = 0; streams[idx]->uid; idx++) {
            if (quiet) {
                json_object_array_add(streamsJ, json_object_new_string(streams[idx]->uid));
            } else {
                json_object *numidJ;
                wrap_json_pack(&numidJ, "{si,si}"
                        , "volume", streams[idx]->volume
                        , "mute", streams[idx]->mute
                        );
                wrap_json_pack(&valueJ, "{ss,so}"
                        , "uid", streams[idx]->uid
                        , "numid", numidJ
                        );
                json_object_array_add(streamsJ, valueJ);
                AFB_ApiWarning(request->dynapi, "stream=%s", json_object_get_string(streamsJ));
            }

        }
        json_object_object_add(responseJ, "streams", streamsJ);
    }

    if (backend || source || zones) {
        AFB_ReqFailF(request, "not implemented", "(Fulup) list action Still To Be Done");
        goto OnErrorExit;
    }

    AFB_ReqSucess(request, responseJ, NULL);
    return;

OnErrorExit:
    AFB_ReqFail(request, "internal-error", "fail to get mixer info");

}

STATIC void MixerAttachVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_request_get_vcbdata(request);
    const char *uid=NULL;
    json_object *playbackJ = NULL, *captureJ = NULL, *zonesJ = NULL, *streamsJ = NULL, *rampsJ = NULL, *loopsJ = NULL;
    json_object *argsJ = afb_request_json(request);
    json_object *responseJ;
    int error;
   
    error = wrap_json_unpack(argsJ, "{ss,s?o,s?o,s?o,s?o,s?o,s?o !}"
            , "uid", &uid
            , "ramps", &rampsJ
            , "playbacks", &playbackJ
            , "captures", &captureJ
            , "loops", &loopsJ
            , "zones", &zonesJ
            , "streams", &streamsJ
            );
    if (error) {
        AFB_ApiError(mixer->api, "MixerAttachVerb: invalid-syntax mixer=%s error=%s args=%s", mixer->uid,  wrap_json_get_error_string(error), json_object_get_string(argsJ));
        AFB_ReqFailF(request, "invalid-syntax", "mixer=%s missing 'uid|ramps|playbacks|captures|zones|streams' error=%s args=%s", mixer->uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    if (playbackJ) {
        error = ApiSinkAttach(mixer, request, uid, playbackJ);
        if (error) goto OnErrorExit;
    }

    if (captureJ) {
        error = ApiSourceAttach(mixer, request, uid, captureJ);
        if (error) goto OnErrorExit;
    }

    if (loopsJ) {
        error = ApiLoopAttach(mixer, request, uid, loopsJ);
        if (error) goto OnErrorExit;
    }

    if (zonesJ) {
        error = ApiZoneAttach(mixer, request, uid, zonesJ);
        if (error) goto OnErrorExit;
    }

    if (rampsJ) {
        error = ApiRampAttach(mixer, request, uid, rampsJ);
        if (error) goto OnErrorExit;
    }

    if (streamsJ) {
        error = ApiStreamAttach(mixer, request, uid, streamsJ, &responseJ);
        if (error) goto OnErrorExit;
    }

    AFB_ReqNotice(request, "**** mixer=%s response=%s", json_object_get_string(responseJ),  mixer->uid);
    AFB_ReqSucess(request, NULL, mixer->uid);
    return;

OnErrorExit:
    return;
}

STATIC void MixerPingVerb(AFB_ReqT request) {
    static int count = 0;
    count++;
    AFB_ReqNotice(request, "Controller:ping count=%d", count);
    AFB_ReqSucess(request, json_object_new_int(count), NULL);

    return;
}

STATIC void MixerEventCB(AFB_ApiT api, const char *evtLabel, struct json_object *eventJ) {

    SoftMixerT *mixer = (SoftMixerT*) afb_dynapi_get_userdata(api);
    assert(mixer);

    AFB_ApiNotice(api, "Mixer=%s Received event=%s, eventJ=%s", mixer->uid, evtLabel, json_object_get_string(eventJ));
}

// Every HAL export the same API & Interface Mapping from SndCard to AudioLogic is done through alsaHalSndCardT
STATIC AFB_ApiVerbs CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping", .callback = MixerPingVerb, .info = "ping count test"},
    { .verb = "attach", .callback = MixerAttachVerb, .info = "attach resources to mixer"},
    { .verb = "remove", .callback = MixerRemoveVerb, .info = "remove existing mixer streams, zones, ..."},
    { .verb = "info", .callback = MixerInfoVerb, .info = "list existing mixer streams, zones, ..."},
    { .verb = NULL} /* marker for end of the array */
};

STATIC int LoadStaticVerbs(SoftMixerT *mixer, AFB_ApiVerbs *verbs) {
    int errcount = 0;

    for (int idx = 0; verbs[idx].verb; idx++) {
        errcount += afb_dynapi_add_verb(mixer->api, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, (void*) mixer, CtrlApiVerbs[idx].auth, 0);
    }

    return errcount;
};

STATIC int MixerInitCB(AFB_ApiT api) {

    SoftMixerT *mixer = (SoftMixerT*) afb_dynapi_get_userdata(api);
    assert(mixer);
    
    // attach AFB mainloop to mixer
    mixer->sdLoop = AFB_GetEventLoop(api);


    AFB_ApiNotice(api, "MixerInitCB API=%s activated info=%s", mixer->uid, mixer->info);

    return 0;
}

STATIC int MixerApiCB(void* handle, AFB_ApiT api) {
    SoftMixerT *mixer = (SoftMixerT*) handle;

    mixer->api= api;
    afb_dynapi_set_userdata(api, mixer);
    afb_dynapi_on_event(api, MixerEventCB);
    afb_dynapi_on_init(api, MixerInitCB);

    int error = LoadStaticVerbs(mixer, CtrlApiVerbs);
    if (error) goto OnErrorExit;
    
    return 0;

OnErrorExit:
    return -1;
}

CTLP_LUA2C(_mixer_new_, source, argsJ, responseJ) {
    SoftMixerT *mixer = calloc(1, sizeof (SoftMixerT));
    int error;
    mixer->max.loops = SMIXER_DEFLT_RAMPS;
    mixer->max.sinks = SMIXER_DEFLT_SINKS;
    mixer->max.sources = SMIXER_DEFLT_SOURCES;
    mixer->max.zones = SMIXER_DEFLT_ZONES;
    mixer->max.streams = SMIXER_DEFLT_STREAMS;
    mixer->max.ramps = SMIXER_DEFLT_RAMPS;

    if (json_object_get_type(argsJ) != json_type_object) {
        AFB_ApiError(source->api, "_mixer_new_: invalid object type= %s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    error = wrap_json_unpack(argsJ, "{ss,s?s,s?i,s?i,s?i,s?i,s?i,s?i !}"
            , "uid", &mixer->uid
            , "info", &mixer->info
            , "max_loop", &mixer->max.loops
            , "max_sink", &mixer->max.sinks
            , "max_source", &mixer->max.sources
            , "max_zone", &mixer->max.zones
            , "max_stream", &mixer->max.streams
            , "max_ramp", &mixer->max.ramps
            );
    if (error) {
        AFB_ApiNotice(source->api, "_mixer_new_ missing 'uid|max_loop|max_sink|max_source|max_zone|max_stream|max_ramp' error=%s mixer=%s", wrap_json_get_error_string(error),json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // make sure string do not get deleted
    mixer->uid = strdup(mixer->uid);
    if (mixer->info)mixer->info = strdup(mixer->info);

    mixer->loops = calloc(mixer->max.loops+1, sizeof (AlsaSndLoopT));
    mixer->sinks = calloc(mixer->max.sinks+1, sizeof (AlsaSndPcmT));
    mixer->sources = calloc(mixer->max.sources+1, sizeof (AlsaSndPcmT));
    mixer->zones = calloc(mixer->max.zones+1, sizeof (AlsaSndZoneT));
    mixer->streams = calloc(mixer->max.streams+1, sizeof (AlsaStreamAudioT));
    mixer->ramps = calloc(mixer->max.ramps+1, sizeof (AlsaVolRampT));

    // create mixer verb within API.
    error = afb_dynapi_new_api(source->api, mixer->uid, mixer->info, !MAINLOOP_CONCURENCY, MixerApiCB, mixer);
    if (error) {
        AFB_ApiError(source->api, "_mixer_new_ mixer=%s fail to Registry API verb", mixer->uid);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

// provide a similar command but for API

CTLP_CAPI(mixer_new, source, argsJ, queryJ) {
    json_object * responseJ;

    // merge static config args with dynamic one coming from the request
    if (argsJ && json_object_get_type(argsJ) == json_type_object) {

        json_object_object_foreach(argsJ, key, val) {
            json_object_get(val);
            json_object_object_add(queryJ, key, val);
        }
    }

    int error = _mixer_new_(source, queryJ, &responseJ);

    if (error)
        AFB_ReqFailF(source->request, "fail-create", "invalid arguments");
    else
        AFB_ReqSucess(source->request, responseJ, NULL);

    return error;
}
