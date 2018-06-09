/*
 * Copyright (C) 2016 "IoT.bzh"
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
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mixer-binding.h"


// default api to print log when apihandle not avaliable
PUBLIC afb_dynapi *AFB_default;

// Config Section definition (note: controls section index should match handle retrieval in HalConfigExec)
static CtlSectionT ctrlSections[]= {
    {.key="resources" , .loadCB= PluginConfig},
    {.key="onload"  , .loadCB= OnloadConfig},
    // {.key="controls", .loadCB= ControlConfig},

    {.key=NULL}
};

STATIC void ctrlapi_ping (AFB_ReqT request) {
    static int count=0;

    count++;
    AFB_ReqNotice (request, "Controller:ping count=%d", count);
    AFB_ReqSuccess(request,json_object_new_int(count), NULL);

    return;
}

// Every HAL export the same API & Interface Mapping from SndCard to AudioLogic is done through alsaHalSndCardT
STATIC AFB_ApiVerbs CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = ctrlapi_ping     , .info = "ping test for API"},
    { .verb = NULL} /* marker for end of the array */
};

STATIC int CtrlLoadStaticVerbs (afb_dynapi *apiHandle, AFB_ApiVerbs *verbs) {
    int errcount=0;

    for (int idx=0; verbs[idx].verb; idx++) {
        errcount+= afb_dynapi_add_verb(apiHandle, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, (void*)&CtrlApiVerbs[idx], CtrlApiVerbs[idx].auth, 0);
    }

    return errcount;
};

// next generation dynamic API-V3 mode
#include <signal.h>


STATIC int CtrlLoadOneApi (void *cbdata, AFB_ApiT apiHandle) {
    CtlConfigT *ctrlConfig = (CtlConfigT*) cbdata;

    // save closure as api's data context
    afb_dynapi_set_userdata(apiHandle, ctrlConfig);
    
    // add static controls verbs
    int error = CtrlLoadStaticVerbs (apiHandle, CtrlApiVerbs);
    if (error) {
        AFB_ApiError(apiHandle, "CtrlLoadSection fail to Registry static V2 verbs");
        goto OnErrorExit;
    }

    // load section for corresponding API
    error= CtlLoadSections(apiHandle, ctrlConfig, ctrlSections);

    // declare an event event manager for this API;
    afb_dynapi_on_event(apiHandle, CtrlDispatchApiEvent);

    // should not seal API as each mixer+stream create a new verb
    // afb_dynapi_seal(apiHandle);
    return error;

OnErrorExit:
    return 1;
}


PUBLIC int afbBindingVdyn(afb_dynapi *apiHandle) {

    AFB_default = apiHandle;
    AFB_ApiNotice (apiHandle, "Controller in afbBindingVdyn");

    const char *dirList= getenv("CONTROL_CONFIG_PATH");
    if (!dirList) dirList=CONTROL_CONFIG_PATH;

    const char *configPath = CtlConfigSearch(apiHandle, dirList, "smixer");
    if (!configPath) {
        AFB_ApiError(apiHandle, "CtlPreInit: No smixer-%s-* config found in %s ", GetBinderName(), dirList);
        goto OnErrorExit;
    }

    // load config file and create API
    CtlConfigT *ctrlConfig = CtlLoadMetaData (apiHandle, configPath);
    if (!ctrlConfig) {
        AFB_ApiError(apiHandle, "CtrlBindingDyn No valid control config file in:\n-- %s", configPath);
        goto OnErrorExit;
    }
    
    if (!ctrlConfig->api) {
        AFB_ApiError(apiHandle, "CtrlBindingDyn API Missing from metadata in:\n-- %s", configPath);
        goto OnErrorExit;
    }
    
    AFB_ApiNotice (apiHandle, "Controller API='%s' info='%s'", ctrlConfig->api, ctrlConfig->info);
    // create one API per config file (Pre-V3 return code ToBeChanged)
    int status = afb_dynapi_new_api(apiHandle, ctrlConfig->api, ctrlConfig->info, 1, CtrlLoadOneApi, ctrlConfig);

    // config exec should be done after api init in order to enable onload to use newly defined ctl API.
    if (!status) 
        status = CtlConfigExec (apiHandle, ctrlConfig);
    
    return status;

OnErrorExit:
    return -1;
}

