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


// Force Lua2cWrapper inclusion within already existing plugin
Lua2cWrapperT Lua2cWrap;

CTLP_LUA2C (AlsaDmix, source, argsJ, responseJ) {
	json_object* subscribeArgsJ = NULL;

	int err = 0;
	wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
	AFB_ApiNotice(source->api, "--lua2c-- AlsaDmix");

	return err;
}

CTLP_LUA2C (AlsaRouter, source, argsJ, responseJ) {
	json_object* subscribeArgsJ = NULL;

	int err = 0;
	wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
	AFB_ApiNotice(source->api, "lua2c router with %s", json_object_to_json_string_ext(subscribeArgsJ, JSON_C_TO_STRING_PRETTY));

	return err;
}
