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


// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;

// API 

static void MixerApiVerbCB(AFB_ReqT request) {
    json_object *valueJ, *backendJ = NULL, *frontendJ = NULL, *zonesJ = NULL, *streamsJ = NULL, *listJ = NULL;
    // retrieve action handle from request and execute the request
    json_object *argsJ = afb_request_json(request);
    json_object *responseJ = json_object_new_object();

    SoftMixerHandleT *mixer = (SoftMixerHandleT*) afb_request_get_vcbdata(request);
    int error;
    int delete = 0;

    CtlSourceT *source = alloca(sizeof (CtlSourceT));
    source->uid = mixer->uid;
    source->api = request->dynapi;
    source->request = request;
    source->context = mixer;

    error = wrap_json_unpack(argsJ, "{s?b,s?o,s?o,s?o,s?o,s?o !}"
            , "delete", &delete
            , "list", &listJ
            , "backend", &backendJ
            , "frontend", &frontendJ
            , "zones", &zonesJ
            , "streams", &streamsJ
            );
    if (error) {
        AFB_ReqFailF(request, "invalid-syntax", "request missing 'uid|list|backend|frontend|zones|streams' mixer=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // Free attached resources and free mixer
    if (delete) {
        for (int idx = 0; mixer->streams[idx].uid; idx++) {
            AlsaLoopStreamT *stream = &mixer->streams[idx];

            AFB_ApiNotice(source->api, "cleaning mixer=%s stream=%s", mixer->uid, stream->uid);

            error = pthread_cancel(stream->copy.thread);
            if (error) {
                AFB_ReqFailF(request, "internal-error", "Fail to kill audio-stream threads mixer=%s", mixer->uid);
                goto OnErrorExit;
            }

            char apiStreamVerb[128];
            error = snprintf(apiStreamVerb, sizeof (apiStreamVerb), "%s/%s", mixer->uid, stream->uid);
            if (error == sizeof (apiStreamVerb)) {
                AFB_ApiError(source->api, "LoopStreams mixer=%s fail to Registry Stream API too long %s/%s", mixer->uid, mixer->uid, stream->uid);
                goto OnErrorExit;
            }

            error = afb_dynapi_sub_verb(source->api, apiStreamVerb);
            if (error) {
                AFB_ApiError(source->api, "fail to Clean API verb=%s", apiStreamVerb);
                goto OnErrorExit;
            }

            // free audio-stream dynamic structures
            snd_pcm_close(mixer->streams[idx].copy.pcmIn);
            snd_pcm_close(mixer->streams[idx].copy.pcmOut);
            if (stream->copy.evtsrc) sd_event_source_unref(stream->copy.evtsrc);
            if (stream->copy.sdLoop) sd_event_unref(stream->copy.sdLoop);

        }

        // registry is attached to frontend
        if (mixer->frontend->registry)free(mixer->frontend->registry);

        error = afb_dynapi_sub_verb(source->api, mixer->uid);
        if (error) {
            AFB_ApiError(source->api, "fail to Clean API verb=%s", mixer->uid);
            goto OnErrorExit;
        }

        // finally free mixer handle
        free(mixer);
        responseJ = json_object_new_string("Fulup: delete might not clean everything properly");
        goto OnSuccessExit;
    }

    if (listJ) {
        int streams = 0, quiet = 0, backend = 0, frontend = 0, zones = 0;

        error = wrap_json_unpack(listJ, "{s?b,s?b,s?b,s?b,s?b !}"
                , "quiet", &quiet
                , "streams", &streams
                , "backend", &backend
                , "frontend", &frontend
                , "zones", &zones
                );
        if (error) {
            AFB_ReqFailF(request, "invalid-syntax", "list missing 'uid|backend|frontend|zones|streams' list=%s", json_object_get_string(listJ));
            goto OnErrorExit;
        }

        if (streams) {
            streamsJ = json_object_new_array();

            AlsaLoopStreamT *streams = mixer->streams;
            for (int idx = 0; streams[idx].uid; idx++) {
                if (quiet) {
                    json_object_array_add(streamsJ, json_object_new_string(streams[idx].uid));
                } else {
                    json_object *numidJ;
                    wrap_json_pack(&numidJ, "{si,si}"
                            , "volume", streams[idx].volume
                            , "mute", streams[idx].mute
                            );
                    wrap_json_pack(&valueJ, "{ss,so}"
                            , "uid", streams[idx].uid
                            , "numid", numidJ
                            );
                    json_object_array_add(streamsJ, valueJ);
                    AFB_ApiWarning(request->dynapi, "stream=%s", json_object_get_string(streamsJ));
                }

            }
            json_object_object_add(responseJ, "streams", streamsJ);
        }

        if (backend || frontend || zones) {
            AFB_ReqFailF(request, "not implemented", "(Fulup) list action Still To Be Done");
            goto OnErrorExit;
        }

        AFB_ReqSucess(request, responseJ, NULL);
        return;
    }

    if (backendJ) {
        error = SndBackend(source, backendJ);
        if (error) goto OnErrorExit;
    }

    if (frontendJ) {
        error = SndFrontend(source, frontendJ);
        if (error) goto OnErrorExit;
    }

    if (zonesJ) {
        error = SndZones(source, zonesJ);
        if (error) goto OnErrorExit;
    }

    if (streamsJ) {
        error = LoopStreams(source, streamsJ, &responseJ);
        if (error) goto OnErrorExit;
    }

OnSuccessExit:
    AFB_ReqSucess(request, responseJ, mixer->uid);
    return;

OnErrorExit:
    return;
}

CTLP_LUA2C(_mixer_new_, source, argsJ, responseJ) {
    SoftMixerHandleT *mixer = calloc(1, sizeof (SoftMixerHandleT));
    json_object *backendJ = NULL, *frontendJ = NULL, *zonesJ = NULL, *streamsJ = NULL;
    int error;
    assert(source->api);

    if (json_object_get_type(argsJ) != json_type_object) {
        AFB_ApiError(source->api, "_mixer_new_: invalid object type= %s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    error = wrap_json_unpack(argsJ, "{ss,s?s,s?o,s?o,s?o,s?o !}"
            , "uid", &mixer->uid
            , "info", &mixer->info
            , "backend", &backendJ
            , "frontend", &frontendJ
            , "zones", &zonesJ
            , "streams", &streamsJ);
    if (error) {
        AFB_ApiNotice(source->api, "_mixer_new_ missing 'uid|backend|frontend|zones|streams' mixer=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // make sure string do not get deleted
    mixer->uid = strdup(mixer->uid);
    if (mixer->info)mixer->info = strdup(mixer->info);

    // create mixer verb within API.
    error = afb_dynapi_add_verb(source->api, mixer->uid, mixer->info, MixerApiVerbCB, mixer, NULL, 0);
    if (error) {
        AFB_ApiError(source->api, "_mixer_new_ mixer=%s fail to Registry API verb", mixer->uid);
        return -1;
    }

    // make sure sub command get access to mixer handle
    source->context = mixer;

    if (backendJ) {
        error = SndBackend(source, backendJ);
        if (error) goto OnErrorExit;
    }

    if (frontendJ) {
        error = SndFrontend(source, frontendJ);
        if (error) goto OnErrorExit;
    }

    if (zonesJ) {
        error = SndZones(source, zonesJ);
        if (error) goto OnErrorExit;
    }

    if (streamsJ) {
        error = LoopStreams(source, streamsJ, responseJ);
        if (error) goto OnErrorExit;
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
