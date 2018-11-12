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
#include "alsa-bluez.h"

#include <string.h>
#include <pthread.h>


extern Lua2cWrapperT Lua2cWrap;

static json_object *LoopsJ = NULL; // AVIRT: temporary loops JSON

static void MixerRemoveVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_req_get_vcbdata(request);
    int error;

    for (int idx = 0; mixer->streams[idx]->uid; idx++) {
        AlsaStreamAudioT *stream = mixer->streams[idx];
        AlsaPcmCopyHandleT * copy = stream->copy;

        AFB_ApiNotice(mixer->api, "cleaning mixer=%s stream=%s", mixer->uid, stream->uid);

        error = pthread_cancel(stream->copy->rthread);
        if (error) {
            AFB_ReqFailF(request, "internal-error", "Fail to kill audio-stream threads mixer=%s", mixer->uid);
            goto OnErrorExit;
        }

        error = afb_api_del_verb(mixer->api, stream->uid, (void**)&mixer);
        if (error) {
            AFB_ReqFailF(request, "internal-error", "Mixer=%s fail to remove verb=%s error=%s", mixer->uid, stream->uid, strerror(error));
            goto OnErrorExit;
        }

        // free audio-stream dynamic structures
        snd_pcm_close(copy->pcmIn->handle);
        snd_pcm_close(copy->pcmOut->handle);
        if (copy->evtsrc) sd_event_source_unref(copy->evtsrc);
        if (copy->sdLoop) sd_event_unref(copy->sdLoop);
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

STATIC json_object *MixerInfoOneStream(AlsaStreamAudioT *stream, int verbose) {
    json_object *alsaJ, *responseJ;


    if (!verbose) {
        wrap_json_pack(&responseJ, "{ss, ss, ss}", "uid", stream->uid, "verb", stream->verb, "alsa", stream->source);
    } else {

        wrap_json_pack(&alsaJ, "{ss,si,si}"
                , "cardid", stream->source
                , "volume", stream->volume
                , "mute", stream->mute
                );
        wrap_json_pack(&responseJ, "{ss,ss,so}"
                , "uid", stream->uid
                , "verb", stream->verb
                , "alsa", alsaJ
                );
    }
    return (responseJ);
}

STATIC json_object * MixerInfoStreams(SoftMixerT *mixer, json_object *streamsJ, int verbose) {
    int error;
    const char * key;
    json_object *valueJ;
    json_object *responseJ = NULL;
    AlsaStreamAudioT **streams = mixer->streams;


    switch (json_object_get_type(streamsJ)) {

        case json_type_null:
        case json_type_boolean:
            // list every existing stream
            responseJ = json_object_new_array();
            for (int idx = 0; streams[idx]; idx++) {
                valueJ = MixerInfoOneStream(streams[idx], verbose);
                json_object_array_add(responseJ, valueJ);
            }
            break;

        case json_type_string:
            key = json_object_get_string(streamsJ);
            for (int idx = 0; streams[idx]; idx++) {
                if (strcasecmp(streams[idx]->uid, key)) continue;
                responseJ = MixerInfoOneStream(streams[idx], verbose);
                break;
            }
            break;

        case json_type_object:
            error = wrap_json_unpack(streamsJ, "{ss}", "uid", &key);
            if (error) {
                AFB_ApiError(mixer->api,
                             "%s: missing 'uid' request streamJ=%s error=%s position=%d",
                             __func__, json_object_get_string(streamsJ), wrap_json_get_error_string(error), wrap_json_get_error_position(error));
                goto OnErrorExit;
            }
            for (int idx = 0; streams[idx]; idx++) {
                if (strcasecmp(streams[idx]->uid, key)) continue;
                responseJ = MixerInfoOneStream(streams[idx], verbose);
                break;
            }
            break;

        case json_type_array:
            responseJ = json_object_new_array();
            for (int idx = 0; idx < json_object_array_length(streamsJ); idx++) {
                json_object *streamJ = json_object_array_get_idx(streamsJ, idx);

                valueJ = MixerInfoStreams(mixer, streamJ, verbose);
                if (!valueJ) {
                    AFB_ApiError(mixer->api,
                                 "%s: fail to find stream=%s",
                                 __func__, json_object_get_string(streamsJ));
                    goto OnErrorExit;
                }
                json_object_array_add(responseJ, valueJ);
            }
            break;

        default:
            AFB_ApiError(mixer->api, "MixerInfoStreams: unsupported json type streamsJ=%s", json_object_get_string(streamsJ));
            goto OnErrorExit;
    }

    return (responseJ);

OnErrorExit:
    return NULL;
}

STATIC json_object *MixerInfoOneRamp(AlsaVolRampT *ramp, int verbose) {
    json_object *responseJ;

    if (!verbose) {
        wrap_json_pack(&responseJ, "{ss}", "uid", ramp->uid);
    } else {
        wrap_json_pack(&responseJ, "{ss,si,si,si}"
                , "uid", ramp->uid
                , "delay", ramp->delay
                , "step_down", ramp->stepDown
                , "step_up", ramp->stepUp
                );
    }
    return (responseJ);
}

STATIC json_object *MixerInfoRamps(SoftMixerT *mixer, json_object *rampsJ, int verbose) {
    int error;
    const char * key;
    json_object *valueJ;
    json_object *responseJ = NULL;
    AlsaVolRampT **ramps = mixer->ramps;

    switch (json_object_get_type(rampsJ)) {

        case json_type_null:
        case json_type_boolean:
            // list every existing ramp
            responseJ = json_object_new_array();
            for (int idx = 0; ramps[idx]; idx++) {

                valueJ = MixerInfoOneRamp(ramps[idx], verbose);
                json_object_array_add(responseJ, valueJ);
            }
            break;

        case json_type_string:
            key = json_object_get_string(rampsJ);
            for (int idx = 0; ramps[idx]; idx++) {
                if (strcasecmp(ramps[idx]->uid, key)) continue;
                responseJ = MixerInfoOneRamp(ramps[idx], verbose);
                break;
            }
            break;

        case json_type_object:
            error = wrap_json_unpack(rampsJ, "{ss}", "uid", &key);
            if (error) {
                AFB_ApiError(mixer->api, "MixerInfoRamps: missing 'uid' request rampJ=%s error=%s position=%d", json_object_get_string(rampsJ), wrap_json_get_error_string(error), wrap_json_get_error_position(error));
                goto OnErrorExit;
            }
            for (int idx = 0; ramps[idx]; idx++) {
                if (strcasecmp(ramps[idx]->uid, key)) continue;
                responseJ = MixerInfoOneRamp(ramps[idx], verbose);
                break;
            }
            break;

        case json_type_array:
            responseJ = json_object_new_array();
            for (int idx = 0; idx < json_object_array_length(rampsJ); idx++) {
                json_object *rampJ = json_object_array_get_idx(rampsJ, idx);

                valueJ = MixerInfoRamps(mixer, rampJ, verbose);
                if (!valueJ) {
                    AFB_ApiError(mixer->api, "MixerInfoRamps: fail to find ramp=%s", json_object_get_string(rampsJ));
                    goto OnErrorExit;
                }
                json_object_array_add(responseJ, valueJ);
            }
            break;

        default:
            AFB_ApiError(mixer->api, "MixerInfoRamps: unsupported json type rampsJ=%s", json_object_get_string(rampsJ));
            goto OnErrorExit;
    }

    return (responseJ);

OnErrorExit:
    return NULL;
}

STATIC json_object *MixerInfoOneZone(AlsaSndZoneT *zone, int verbose) {
    json_object *responseJ;

    if (!verbose) {
        wrap_json_pack(&responseJ, "{ss}", "uid", zone->uid);
    } else {
        json_object *responseJ = json_object_new_object();
        if (zone->sinks) {
            json_object *sinksJ = json_object_new_array();
            for (int jdx = 0; zone->sinks[jdx]; jdx++) {
                json_object *channelJ;
                wrap_json_pack(&channelJ, "{ss,si}"
                        , "uid", zone->sinks[jdx]->uid
                        , "port", zone->sinks[jdx]->port
                        );
                json_object_array_add(sinksJ, channelJ);
            }
            json_object_object_add(responseJ, "sinks", sinksJ);
        }

        if (zone->sources) {
            json_object *sourcesJ = json_object_new_array();
            for (int jdx = 0; zone->sources[jdx]; jdx++) {
                json_object *channelJ;
                wrap_json_pack(&channelJ, "{ss,si}"
                        , "uid", zone->sources[jdx]->uid
                        , "port", zone->sources[jdx]->port
                        );
                json_object_array_add(sourcesJ, channelJ);
            }
            json_object_object_add(responseJ, "source", sourcesJ);
        }

        if (zone->params) {
            json_object *paramsJ;
            wrap_json_pack(&paramsJ, "{si,ss,si}"
                    , "rate", zone->params->rate
                    , "format", zone->params->formatS
                    , "channels", zone->params->channels
                    );
            json_object_object_add(responseJ, "params", paramsJ);
        }
    }
    return (responseJ);
}

STATIC json_object *MixerInfoZones(SoftMixerT *mixer, json_object *zonesJ, int verbose) {
    int error;
    const char * key;
    json_object *valueJ;
    json_object *responseJ = NULL;
    AlsaSndZoneT **zones = mixer->zones;

    switch (json_object_get_type(zonesJ)) {

        case json_type_null:
        case json_type_boolean:
            // list every existing zone
            responseJ = json_object_new_array();
            for (int idx = 0; zones[idx]; idx++) {

                valueJ = MixerInfoOneZone(zones[idx], verbose);
                json_object_array_add(responseJ, valueJ);
            }
            break;

        case json_type_string:
            key = json_object_get_string(zonesJ);
            for (int idx = 0; zones[idx]; idx++) {
                if (strcasecmp(zones[idx]->uid, key)) continue;
                responseJ = MixerInfoOneZone(zones[idx], verbose);
                break;
            }
            break;

        case json_type_object:
            error = wrap_json_unpack(zonesJ, "{ss}", "uid", &key);
            if (error) {
                AFB_ApiError(mixer->api,
                             "%s: missing 'uid' request zoneJ=%s error=%s position=%d",
                             __func__ ,json_object_get_string(zonesJ), wrap_json_get_error_string(error), wrap_json_get_error_position(error));
                goto OnErrorExit;
            }
            for (int idx = 0; zones[idx]; idx++) {
                if (strcasecmp(zones[idx]->uid, key)) continue;
                responseJ = MixerInfoOneZone(zones[idx], verbose);
                break;
            }
            break;

        case json_type_array:
            responseJ = json_object_new_array();
            for (int idx = 0; idx < json_object_array_length(zonesJ); idx++) {
                json_object *zoneJ = json_object_array_get_idx(zonesJ, idx);

                valueJ = MixerInfoZones(mixer, zoneJ, verbose);
                if (!valueJ) {
                    AFB_ApiError(mixer->api, "MixerInfoZones: fail to find zone=%s", json_object_get_string(zonesJ));
                    goto OnErrorExit;
                }
                json_object_array_add(responseJ, valueJ);
            }
            break;

        default:
            AFB_ApiError(mixer->api, "MixerInfoZones: unsupported json type zonesJ=%s", json_object_get_string(zonesJ));
            goto OnErrorExit;
    }

    AFB_ApiNotice(mixer->api, "MixerInfoZones: response=%s", json_object_get_string(responseJ));
    return (responseJ);

OnErrorExit:
    return NULL;
}

STATIC json_object *MixerInfoOnePcm(AlsaSndPcmT *pcm, int verbose) {
    json_object *responseJ;

    if (!verbose) {
        wrap_json_pack(&responseJ, "{ss,ss}", "uid", pcm->uid, "verb", pcm->verb);
    } else {
        json_object *sndcardJ, *alsaJ;
        wrap_json_pack(&sndcardJ, "{ss,si,si,si}"
                ,"cardid", pcm->sndcard->cid.cardid
                ,"name", pcm->sndcard->cid.name
                ,"longname", pcm->sndcard->cid.longname
                ,"index", pcm->sndcard->cid.cardidx
                ,"device", pcm->sndcard->cid.device
                ,"subdev", pcm->sndcard->cid.subdev
                );

        wrap_json_pack(&alsaJ, "{ss,ss,so}"
                , "volume", pcm->volume
                , "mute", pcm->mute
                , "ccount", pcm->ccount
                );
        wrap_json_pack(&responseJ, "{ss,ss,so,so}"
                , "uid", pcm->uid
                , "verb", pcm->verb
                , "sndcard", sndcardJ
                , "alsa", alsaJ
                );
    }
    return (responseJ);
}

STATIC json_object *MixerInfoPcms(SoftMixerT *mixer, json_object *pcmsJ, snd_pcm_stream_t direction, int verbose) {
    int error;
    const char * key;
    json_object *valueJ;
    json_object *responseJ = NULL;
    AlsaSndPcmT **pcms;

    switch (direction) {
        case SND_PCM_STREAM_PLAYBACK:
            pcms = mixer->sinks;
            break;

        case SND_PCM_STREAM_CAPTURE:
            pcms = mixer->sources;
            break;
        default:
            AFB_ApiError(mixer->api, "MixerInfoPcms: invalid Direction should be SND_PCM_STREAM_PLAYBACK|SND_PCM_STREAM_capture");
            goto OnErrorExit;
    }

    switch (json_object_get_type(pcmsJ)) {

        case json_type_null:
        case json_type_boolean:
            // list every existing pcm
            responseJ = json_object_new_array();
            for (int idx = 0; pcms[idx]; idx++) {

                valueJ = MixerInfoOnePcm(pcms[idx], verbose);
                json_object_array_add(responseJ, valueJ);
            }
            break;

        case json_type_string:
            key = json_object_get_string(pcmsJ);
            for (int idx = 0; pcms[idx]; idx++) {
                if (strcasecmp(pcms[idx]->uid, key)) continue;
                responseJ = MixerInfoOnePcm(pcms[idx], verbose);
                break;
            }
            break;

        case json_type_object:
            error = wrap_json_unpack(pcmsJ, "{ss}", "uid", &key);
            if (error) {
                AFB_ApiError(mixer->api,
                             "%s: missing 'uid' request pcmJ=%s error=%s position=%d",
                             __func__, json_object_get_string(pcmsJ), wrap_json_get_error_string(error), wrap_json_get_error_position(error));
                goto OnErrorExit;
            }
            for (int idx = 0; pcms[idx]; idx++) {
                if (strcasecmp(pcms[idx]->uid, key)) continue;
                responseJ = MixerInfoOnePcm(pcms[idx], verbose);
                break;
            }
            break;

        case json_type_array:
            responseJ = json_object_new_array();
            for (int idx = 0; idx < json_object_array_length(pcmsJ); idx++) {
                json_object *pcmJ = json_object_array_get_idx(pcmsJ, idx);

                valueJ = MixerInfoPcms(mixer, pcmJ, direction, verbose);
                if (!valueJ) {
                    AFB_ApiError(mixer->api, "%s: fail to find %s=%s",
                                 __func__, direction==SND_PCM_STREAM_PLAYBACK?"playback":"capture", json_object_get_string(pcmJ));
                    goto OnErrorExit;
                }
                json_object_array_add(responseJ, valueJ);
            }
            break;

        default:
            AFB_ApiError(mixer->api,
                         "%s: unsupported json type pcmsJ=%s",
                         __func__, json_object_get_string(pcmsJ));
            goto OnErrorExit;
    }

    return (responseJ);

OnErrorExit:
    return NULL;
}

STATIC void MixerInfoAction(AFB_ReqT request, json_object * argsJ) {

    SoftMixerT *mixer = (SoftMixerT*) afb_req_get_vcbdata(request);
    int error, verbose = 0;
    json_object *streamsJ = NULL, *rampsJ = NULL, *zonesJ = NULL, *capturesJ = NULL, *playbacksJ = NULL;

    error = wrap_json_unpack(argsJ, "{s?b,s?o,s?o,s?o,s?o,s?o !}"
            , "verbose", &verbose
            , "streams", &streamsJ
            , "ramps", &rampsJ
            , "captures", &capturesJ
            , "playbacks", &playbacksJ
            , "zones", &zonesJ
            );
    if (error) {
        AFB_ReqFailF(request, "invalid-syntax", "list missing 'verbose|streams|ramps|captures|playbacks|zones' argsJ=%s", json_object_get_string(argsJ));
        return;
    }

    json_object *responseJ = json_object_new_object();

    if (streamsJ) {
        json_object *resultJ = MixerInfoStreams(mixer, streamsJ, verbose);
        if (!resultJ) {
            AFB_ReqFailF(request, "not-found", "fail to find streams (should be a boolean, a string, or an array of them) argsJ=%s", json_object_get_string(streamsJ));
            return;
        }
        json_object_object_add(responseJ, "streams", resultJ);
    }

    if (rampsJ) {
        json_object *resultJ = MixerInfoRamps(mixer, rampsJ, verbose);
        json_object_object_add(responseJ, "ramps", resultJ);
    }

    if (zonesJ) {
        json_object *resultJ = MixerInfoZones(mixer, zonesJ, verbose);
        json_object_object_add(responseJ, "zones", resultJ);
    }

    if (playbacksJ) {
        json_object *resultJ = MixerInfoPcms(mixer, playbacksJ, SND_PCM_STREAM_PLAYBACK, verbose);
        json_object_object_add(responseJ, "playbacks", resultJ);
    }

    if (capturesJ) {
        json_object *resultJ = MixerInfoPcms(mixer, capturesJ, SND_PCM_STREAM_CAPTURE, verbose);
        json_object_object_add(responseJ, "captures", resultJ);
    }

    AFB_ReqSuccess(request, responseJ, NULL);
    return;
}

STATIC void MixerInfoVerb(AFB_ReqT request) {
    json_object *argsJ = afb_req_json(request);
    MixerInfoAction(request, argsJ);
}

STATIC void MixerAttachVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_req_get_vcbdata(request);
    const char *uid = NULL, *prefix = NULL;
    json_object *playbacksJ = NULL, *capturesJ = NULL, *zonesJ = NULL, *streamsJ = NULL, *rampsJ = NULL, *loopsJ = NULL;
    json_object *argsJ = afb_req_json(request);
    json_object *responseJ = json_object_new_object();
    int error;

    error = wrap_json_unpack(argsJ, "{ss,s?s,s?s,s?o,s?o,s?o,s?o,s?o,s?o !}"
            , "uid", &uid
            , "prefix", &prefix
            , "mixerapi", NULL
            , "ramps", &rampsJ
            , "playbacks", &playbacksJ
            , "captures", &capturesJ
            , "loops", &loopsJ
            , "zones", &zonesJ
            , "streams", &streamsJ
            );
    if (error) {
        AFB_ReqFailF(request,
                     "invalid-syntax",
					 "mixer=%s missing 'uid|ramps|playbacks|captures|zones|streams' error=%s args=%s",
                     mixer->uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    AFB_ApiInfo(mixer->api, "%s set PLAYBACK", __func__);

    if (playbacksJ) {
        error = ApiSinkAttach(mixer, request, uid, playbacksJ);
        if (error) goto OnErrorExit;

        json_object *resultJ = MixerInfoPcms(mixer, playbacksJ, SND_PCM_STREAM_PLAYBACK, 0);
        json_object_object_add(responseJ, "playbacks", resultJ);
    }

    AFB_ApiInfo(mixer->api, "%s set CAPTURE", __func__);

    if (capturesJ) {
        error = ApiSourceAttach(mixer, request, uid, capturesJ);
        if (error) {
            AFB_ApiError(mixer->api,"%s: source attach failed", __func__);
            goto OnErrorExit;
        }

        json_object *resultJ = MixerInfoPcms(mixer, capturesJ, SND_PCM_STREAM_CAPTURE, 0);
        json_object_object_add(responseJ, "captures", resultJ);
    }

    AFB_ApiInfo(mixer->api, "%s set ZONES", __func__);

    if (zonesJ) {
        error = ApiZoneAttach(mixer, request, uid, zonesJ);
        if (error) goto OnErrorExit;
        
        json_object *resultJ = MixerInfoZones(mixer, zonesJ, 0);
        json_object_object_add(responseJ, "zone", resultJ);
    }

    // In AVIRT mode, we require both the loops and streams JSON objects to
    // construct the loopbacks, so when the loops are set, but the streams
    // are not, we need to save the loops until the streams are given to us
    if (streamsJ && (loopsJ || LoopsJ)) {
        AFB_ApiInfo(mixer->api, "%s set LOOPS/AVIRT", __func__);
        error = ApiLoopAttach(mixer, request, uid,
                              ((loopsJ) ? loopsJ : LoopsJ), streamsJ);
        if (error) goto OnErrorExit;
    }

    AFB_ApiInfo(mixer->api, "%s set RAMPS", __func__);

    if (rampsJ) {
        error = ApiRampAttach(mixer, request, uid, rampsJ);
        if (error) goto OnErrorExit;

        json_object *resultJ = MixerInfoRamps(mixer, rampsJ, 0);
        json_object_object_add(responseJ, "ramps", resultJ);
    }

    AFB_ApiInfo(mixer->api,"%s set STREAMS", __func__);

    if (streamsJ) {
        error = ApiStreamAttach(mixer, request, uid, prefix, streamsJ);
        if (error) goto OnErrorExit;

        json_object *resultJ = MixerInfoStreams(mixer, streamsJ, 0);
        json_object_object_add(responseJ, "streams", resultJ);
    }

    AFB_ApiNotice(mixer->api, "%s responseJ=%s", __func__, json_object_get_string(responseJ));
    AFB_ReqSuccess(request, responseJ, NULL);

    AFB_ApiInfo(mixer->api,"%s DONE", __func__);
    return;

OnErrorExit:
	AFB_ApiError(mixer->api,"%s FAILED", __func__);
    return;
}


static void MixerBluezAlsaDevVerb(AFB_ReqT request) {
    SoftMixerT *mixer = (SoftMixerT*) afb_req_get_vcbdata(request);
    char * interface = NULL, *device = NULL, *profile = NULL;
    json_object *argsJ = afb_req_json(request);
    int error;

    if (json_object_is_type(argsJ,json_type_null)) {
    	goto parsed;
    }

    error = wrap_json_unpack(argsJ, "{ss,ss,ss !}"
            , "interface", &interface
            , "device", &device
            , "profile", &profile
            );

    if (error) {
        AFB_ReqFailF(request,
                     "invalid-syntax",
					 "mixer=%s missing 'interface|device|profile' error=%s args=%s",
                     mixer->uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

parsed:
	AFB_ApiNotice(mixer->api, "%s: interface %s, device %s, profile %s\n", __func__, interface, device, profile);
    error = alsa_bluez_set_remote_device(interface, device, profile);
    if (error) {
    	AFB_ReqFailF(request,
    	             "runtime error",
					 "Unable to set device , err %d", error);
    	goto OnErrorExit;
    }

    AFB_ReqSuccess(request, NULL, NULL);

OnErrorExit:
    return;
}

// Every HAL export the same API & Interface Mapping from SndCard to AudioLogic is done through alsaHalSndCardT
STATIC AFB_ApiVerbs CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "attach", .callback = MixerAttachVerb, .info = "attach resources to mixer"},
    { .verb = "remove", .callback = MixerRemoveVerb, .info = "remove existing mixer streams, zones, ..."},
    { .verb = "info", .callback = MixerInfoVerb, .info = "list existing mixer streams, zones, ..."},
	{ .verb = "bluezalsa_dev", .callback = MixerBluezAlsaDevVerb, .info = "set bluez alsa device"},
    { .verb = NULL} /* marker for end of the array */
};

STATIC int LoadStaticVerbs(SoftMixerT *mixer, AFB_ApiVerbs * verbs) {
    int errcount = 0;

    for (int idx = 0; verbs[idx].verb; idx++) {
        errcount += afb_api_add_verb(mixer->api, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, (void*) mixer, CtrlApiVerbs[idx].auth, 0, 0);
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

    // In AVIRT mode, we require both the loops and streams JSON objects to
    // construct the loopbacks, so when the loops are set, but the streams
    // are not, we need to save the loops until the streams are given to us
    if (loopsJ && streamsJ) {
        error = ApiLoopAttach(mixer, NULL, uid, loopsJ, streamsJ);
        if (error) goto OnErrorExit;
    } else {
      LoopsJ = loopsJ;
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
    afb_api_set_userdata(source->api, mixer);

    error = LoadStaticVerbs(mixer, CtrlApiVerbs);
    if (error) goto OnErrorExit;

    return 0;

OnErrorExit:
    return -1;
}


