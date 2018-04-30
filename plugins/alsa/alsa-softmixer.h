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

#ifndef _ALSA_SOFTMIXER_
#define _ALSA_SOFTMIXER_

#include <afb/afb-binding.h>
#include <systemd/sd-event.h>
#include <json-c/json_object.h>
#include <stdbool.h>
#include <string.h>

#include "ctl-plugin.h"
#include "wrap-json.h"

#include <alsa/asoundlib.h>

// Provide proto for LibASound low level API
int _snd_pcm_dmix_open(snd_pcm_t **pcmp, const char *name,
		       snd_config_t *root, snd_config_t *conf,
		       snd_pcm_stream_t stream, int mode);

// alsa-tools-high.c
PUBLIC void AlsaDumpConfig (CtlSourceT *source, snd_config_t *config, int indent);

// alsa-tools-low.c
PUBLIC int  AlsaCardInfoByPath (const char *control);


PUBLIC int AlsaCreateDmix (CtlSourceT *source);

#endif