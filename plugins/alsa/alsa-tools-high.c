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


PUBLIC void AlsaDumpConfig(CtlSourceT *source, snd_config_t *config, int indent) {
    snd_config_iterator_t it, next;

    // hugly hack to get minimalist indentation
    char *pretty = alloca(indent + 1);
    for (int idx = 0; idx < indent; idx++) pretty[idx] = '-';
    pretty[indent] = '\0';

    snd_config_for_each(it, next, config) {
        snd_config_t *node = snd_config_iterator_entry(it);
        const char *key;

        // ignore comment en empty lines
        if (snd_config_get_id(node, &key) < 0) continue;

        switch (snd_config_get_type(node)) {
                long valueI;
                const char *valueS;

            case SND_CONFIG_TYPE_INTEGER:
                snd_config_get_integer(node, &valueI);
                AFB_ApiNotice(source->api, "DumpAlsaConfig: %s %s: %d (int)", pretty, key, (int) valueI);
                break;

            case SND_CONFIG_TYPE_STRING:
                snd_config_get_string(node, &valueS);
                AFB_ApiNotice(source->api, "DumpAlsaConfig: %s %s: %s (str)", pretty, key, valueS);
                break;

            case SND_CONFIG_TYPE_COMPOUND:
                AFB_ApiNotice(source->api, "DumpAlsaConfig: %s %s { ", pretty, key);
                AlsaDumpConfig(source, node, indent + 2);
                AFB_ApiNotice(source->api, "DumpAlsaConfig: %s } ", pretty);
                break;

            default:
                snd_config_get_string(node, &valueS);
                AFB_ApiNotice(source->api, "DumpAlsaConfig: %s: key=%s unknown=%s", pretty, key, valueS);
                break;
        }
    }
}
