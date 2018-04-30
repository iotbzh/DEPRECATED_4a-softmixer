/*
 * Copyright (C) 2017 "IoT.bzh"
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

CTLP_CAPI_REGISTER("alsa-mixer");

// Call at initialisation time
CTLP_ONLOAD(plugin, callbacks) {
	AFB_ApiDebug (plugin->api, "SoftMixer plugin: uid='%s' 'info='%s'", plugin->uid, plugin->info);
        
        // fake action call during init for debug
        CtlSourceT source;
        source.api = plugin->api;
        AlsaCreateDmix (&source);
        
	return 0;
}

CTLP_CAPI (zone_ctl, source, argsJ, eventJ) {
	json_object* subscribeArgsJ = NULL;

	int err = 0;
	wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
	AFB_ApiDebug(source->api, "Calling zone_ctl with %s", json_object_to_json_string_ext(subscribeArgsJ, JSON_C_TO_STRING_PRETTY));

	return err;
}

CTLP_CAPI (stream_ctl, source, argsJ, eventJ) {
	json_object* subscribeArgsJ = NULL;

	int err = 0;
	wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
	AFB_ApiDebug(source->api, "Calling stream_ctl with %s", json_object_to_json_string_ext(subscribeArgsJ, JSON_C_TO_STRING_PRETTY));

	return err;
}
