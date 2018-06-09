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
    AFB_ReqSuccess(request, NULL, "Fulup: delete might not clean everything properly");

    return;

OnErrorExit:
    AFB_ReqFail(request, "internal-error", "fail to delete mixer");
}

STATIC json_object * MixerInfoStreams(SoftMixerT *mixer, json_object *streamsJ, int verbose) {
    const char * key;
    json_object *valueJ;
    json_object *responseJ = json_object_new_array();
    AlsaStreamAudioT **streams = mixer->streams;

    switch (json_object_get_type(streamsJ)) {

        case json_type_null:
        case json_type_boolean:

            for (int idx = 0; streams[idx]; idx++) {
                json_object *alsaJ;

                if (!verbose) {
                    wrap_json_pack(&alsaJ, "{ss}", "alsa", streams[idx]->source);
                    json_object_array_add(responseJ, alsaJ);
                } else {

                    wrap_json_pack(&alsaJ, "{ss,si,si}"
                            , "cardid", streams[idx]->source
                            , "volume", streams[idx]->volume
                            , "mute", streams[idx]->mute
                            );
                    wrap_json_pack(&valueJ, "{ss,ss,so}"
                            , "uid", streams[idx]->uid
                            , "verb", streams[idx]->verb
                            , "alsa", alsaJ
                            );
                    json_object_array_add(responseJ, valueJ);
                }
            }
            break;

        case json_type_string:

            key = json_object_get_string(streamsJ);
            for (int idx = 0; streams[idx]; idx++) {
                json_object *alsaJ;

                if (!strcasestr(streams[idx]->uid, key)) continue;

                if (!verbose) {
                    wrap_json_pack(&alsaJ, "{ss}", "alsa", streams[idx]->source);
                    json_object_array_add(responseJ, alsaJ);
                } else {

                    wrap_json_pack(&alsaJ, "{ss,si,si}"
                            , "cardid", streams[idx]->source
                            , "volume", streams[idx]->volume
                            , "mute", streams[idx]->mute
                            );
                    wrap_json_pack(&valueJ, "{ss,ss,so}"
                            , "uid", streams[idx]->uid
                            , "verb", streams[idx]->verb
                            , "alsa", alsaJ
                            );
                    json_object_array_add(responseJ, valueJ);
                }
            }
            break;
            
        case json_type_array:
            
            for (int idx=0; idx < json_object_array_length(streamsJ); idx++) {
                json_object *streamJ = json_object_array_get_idx(streamsJ, idx);
                valueJ= MixerInfoStreams (mixer, streamJ, verbose);
                json_object_array_add(responseJ, valueJ);
            }

        default:
            goto OnErrorExit;
    }
    return (responseJ);

OnErrorExit:
    return NULL;
}

STATIC json_object * MixerInfoRamps(SoftMixerT *mixer, int verbose) {

    json_object *rampsJ = json_object_new_array();
    json_object *valueJ;

    AlsaVolRampT **ramps = mixer->ramps;
    for (int idx = 0; ramps[idx]; idx++) {
        if (!verbose) {
            json_object_array_add(rampsJ, json_object_new_string(ramps[idx]->uid));
        } else {
            wrap_json_pack(&valueJ, "{ss,si,si,si}"
                    , "uid", ramps[idx]->uid
                    , "delay", ramps[idx]->delay
                    , "step_down", ramps[idx]->stepDown
                    , "step_up", ramps[idx]->stepUp
                    );
            json_object_array_add(rampsJ, valueJ);
        }
    }

    return (rampsJ);
}

STATIC void MixerInfoAction(AFB_ReqT request, json_object * argsJ) {

    SoftMixerT *mixer = (SoftMixerT*) afb_request_get_vcbdata(request);
    int error, verbose = 0, ramps = 0, zones = 0, captures = 0, playbacks = 0;
    json_object *streamsJ = NULL;

    if (json_object_get_type(argsJ) == json_type_null) {
        streamsJ = json_object_new_boolean(1);
        ramps = 1;
        zones = 0;
        captures = 0;
        playbacks = 0;
    } else {
        error = wrap_json_unpack(argsJ, "{s?b,s?o,s?b,s?b,s?b,s?b !}"
                , "verbose", &verbose
                , "streams", &streamsJ
                , "ramps", &ramps
                , "captures", &captures
                , "playbacks", &playbacks
                , "zones", &zones
                );
        if (error) {
            AFB_ReqFailF(request, "invalid-syntax", "list missing 'quiet|stream|backend|source' argsJ=%s", json_object_get_string(argsJ));
            goto OnErrorExit;
        }
    }
    json_object *responseJ = json_object_new_object();

    if (streamsJ) {
        json_object *resultJ = MixerInfoStreams(mixer, streamsJ, verbose);
        if (!resultJ) {
            AFB_ReqFailF(request, "invalid-object", "streams should be boolean or string argsJ=%s", json_object_get_string(streamsJ));
            goto OnErrorExit;
        }
        json_object_object_add(responseJ, "streams", resultJ);
    }

    if (ramps) {
        json_object *rampsJ = MixerInfoRamps (mixer, verbose);
        json_object_object_add(responseJ, "ramps", rampsJ);
    }

    if (zones) {
        json_object *zonesJ = json_object_new_array();

        AlsaSndZoneT **zones = mixer->zones;
        for (int idx = 0; zones[idx]; idx++) {
            if (!verbose) {
                json_object_array_add(zonesJ, json_object_new_string(zones[idx]->uid));
            } else {
                json_object *zoneJ = json_object_new_object();
                if (zones[idx]->sinks) {
                    json_object *sinksJ = json_object_new_array();
                    for (int jdx = 0; zones[idx]->sinks[jdx]; jdx++) {
                        json_object *channelJ;
                        wrap_json_pack(&channelJ, "{ss,si}"
                                , "uid", zones[idx]->sinks[jdx]->uid
                                , "port", zones[idx]->sinks[jdx]->port
                                );
                        json_object_array_add(sinksJ, channelJ);
                    }
                    json_object_object_add(zoneJ, "sinks", sinksJ);
                }

                if (zones[idx]->sources) {
                    json_object *sourcesJ = json_object_new_array();
                    for (int jdx = 0; zones[idx]->sources[jdx]; jdx++) {
                        json_object *channelJ;
                        wrap_json_pack(&channelJ, "{ss,si}"
                                , "uid", zones[idx]->sources[jdx]->uid
                                , "port", zones[idx]->sources[jdx]->port
                                );
                        json_object_array_add(sourcesJ, channelJ);
                    }
                    json_object_object_add(zoneJ, "source", sourcesJ);
                }

                if (zones[idx]->params) {

                    json_object *paramsJ;
                    wrap_json_pack(&paramsJ, "{si,ss,si}"
                            , "rate", zones[idx]->params->rate
                            , "format", zones[idx]->params->formatS
                            , "channels", zones[idx]->params->channels
                            );
                    json_object_object_add(zoneJ, "params", paramsJ);
                }
                json_object_array_add(zonesJ, zoneJ);
            }
        }
        json_object_object_add(responseJ, "zones", zonesJ);
    }

    if (captures || playbacks) {
        AFB_ReqFailF(request, "not implemented", "(Fulup) list action Still To Be Done");
        goto OnErrorExit;
    }

    AFB_ReqSuccess(request, responseJ, NULL);
    return;

OnErrorExit:
    AFB_ReqFail(request, "internal-error", "fail to get mixer info");
}

STATIC void MixerInfoVerb(AFB_ReqT request) {
    json_object *argsJ = afb_request_json(request);
    MixerInfoAction(request, argsJ);
}

STATIC void MixerAttachVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_request_get_vcbdata(request);
    const char *uid = NULL, *prefix = NULL;
    json_object *playbackJ = NULL, *captureJ = NULL, *zonesJ = NULL, *streamsJ = NULL, *rampsJ = NULL, *loopsJ = NULL;
    json_object *argsJ = afb_request_json(request);
    json_object *responseJ = json_object_new_object();
    int error;

    error = wrap_json_unpack(argsJ, "{ss,s?s,s?o,s?o,s?o,s?o,s?o,s?o !}"
            , "uid", &uid
            , "prefix", &prefix
            , "ramps", &rampsJ
            , "playbacks", &playbackJ
            , "captures", &captureJ
            , "loops", &loopsJ
            , "zones", &zonesJ
            , "streams", &streamsJ
            );
    if (error) {
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

        json_object *resultJ = MixerInfoStreams(mixer, streamsJ, 0);
        json_object_object_add(responseJ, "ramps", resultJ);
    }

    if (streamsJ) {
        error = ApiStreamAttach(mixer, request, uid, prefix, streamsJ);
        if (error) goto OnErrorExit;

        json_object *resultJ = MixerInfoStreams(mixer, streamsJ, 0);
        json_object_object_add(responseJ, "streams", resultJ);
    }

    AFB_ReqSuccess(request, responseJ, NULL);

    return;

OnErrorExit:
    return;
}

STATIC void MixerPingVerb(AFB_ReqT request) {
    static int count = 0;
    count++;
    AFB_ReqNotice(request, "Controller:ping count=%d", count);
    AFB_ReqSuccess(request, json_object_new_int(count), NULL);

    return;
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

STATIC int LoadStaticVerbs(SoftMixerT *mixer, AFB_ApiVerbs * verbs) {
    int errcount = 0;

    for (int idx = 0; verbs[idx].verb; idx++) {
        errcount += afb_dynapi_add_verb(mixer->api, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, (void*) mixer, CtrlApiVerbs[idx].auth, 0);
    }

    return errcount;
};

CTLP_CAPI(MixerAttach, source, argsJ, responseJ) {
    SoftMixerT *mixer = source->context;
    json_object *playbackJ = NULL, *captureJ = NULL, *zonesJ = NULL, *streamsJ = NULL, *rampsJ = NULL, *loopsJ = NULL;
    const char* uid = source->uid, *prefix = NULL;

    int error;

    error = wrap_json_unpack(argsJ, "{s?s, s?o,s?o,s?o,s?o,s?o,s?o !}"
            , "prefix", &rampsJ
            , "ramps", &rampsJ
            , "playbacks", &playbackJ
            , "captures", &captureJ
            , "loops", &loopsJ
            , "zones", &zonesJ
            , "streams", &streamsJ
            );
    if (error) {
        AFB_ApiError(mixer->api, "MixerAttachVerb: invalid-syntax mixer=%s error=%s args=%s", mixer->uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    if (playbackJ) {
        error = ApiSinkAttach(mixer, NULL, uid, playbackJ);
        if (error) goto OnErrorExit;
    }

    if (captureJ) {
        error = ApiSourceAttach(mixer, NULL, uid, captureJ);
        if (error) goto OnErrorExit;
    }

    if (loopsJ) {
        error = ApiLoopAttach(mixer, NULL, uid, loopsJ);
        if (error) goto OnErrorExit;
    }

    if (zonesJ) {
        error = ApiZoneAttach(mixer, NULL, uid, zonesJ);
        if (error) goto OnErrorExit;
    }

    if (rampsJ) {
        error = ApiRampAttach(mixer, NULL, uid, rampsJ);
        if (error) goto OnErrorExit;
    }

    if (streamsJ) {
        error = ApiStreamAttach(mixer, NULL, uid, prefix, streamsJ);
        if (error) goto OnErrorExit;
    }

    // return mixer info data after attach
    return 0;

OnErrorExit:
    return -1;
}

CTLP_CAPI(MixerCreate, source, argsJ, responseJ) {
    SoftMixerT *mixer = calloc(1, sizeof (SoftMixerT));
    source->context = mixer;

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
        AFB_ApiNotice(source->api, "_mixer_new_ missing 'uid|max_loop|max_sink|max_source|max_zone|max_stream|max_ramp' error=%s mixer=%s", wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // make sure string do not get deleted
    mixer->uid = strdup(mixer->uid);
    if (mixer->info)mixer->info = strdup(mixer->info);

    mixer->loops = calloc(mixer->max.loops + 1, sizeof (void*));
    mixer->sinks = calloc(mixer->max.sinks + 1, sizeof (void*));
    mixer->sources = calloc(mixer->max.sources + 1, sizeof (void*));
    mixer->zones = calloc(mixer->max.zones + 1, sizeof (void*));
    mixer->streams = calloc(mixer->max.streams + 1, sizeof (void*));
    mixer->ramps = calloc(mixer->max.ramps + 1, sizeof (void*));

    mixer->sdLoop = AFB_GetEventLoop(source->api);
    mixer->api = source->api;
    afb_dynapi_set_userdata(source->api, mixer);

    error = LoadStaticVerbs(mixer, CtrlApiVerbs);
    if (error) goto OnErrorExit;

    return 0;

OnErrorExit:
    return -1;
}