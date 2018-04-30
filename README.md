Softmixer controller for 4A (AGL Advance Audio Architecture).
------------------------------------------------------------

 * Object: Simulate a hardware mixer through and Alsa-Loop driver and a user space mixer
 * Status: In Progress
 * Author: Fulup Ar Foll fulup@iot.bzh
 * Date  : April-2018

## Functionalities:
 - Create an application dedicate controller from a JSON config file
 - Each controls (eg: navigation, multimedia, ...) is a suite of actions. When all actions succeed control is granted, if one fail control acces is denied.
 - Actions can either be:
   + Invocation to an other binding API, either internal or external (eg: a policy service, Alsa UCM, ...)
   + C routines from a user provider plugin (eg: policy routine, proprietary code, ...)
   + Lua script function. Lua provides access to every AGL appfw functionalities and can be extended from C user provided plugins.

## Installation
 - Controler is a native part of AGL Advance Audio Framework but may be used independently with any other service or application binder.
 - Dependencies: the only dependencies are audio-common for JSON-WRAP and Filescan-utils capabilities.
 - Controler relies on Lua-5.3, when not needed Lua might be removed at compilation time.

## Monitoring
 - Default test HTML page expect monitoring HTML page to be accessible from /monitoring for this to work you should
 * place monitoring HTML pages in a well known location eg: $HOME/opt/monitoring
 * start your binder with the alias option e.g. afb-daemon --port=1234 --alias=/monitoring:/home/fulup/opt/afb-monitoring --ldpaths=. --workdir=. --roothttp=../htdocs

## Config

Configuration is loaded dynamically during startup time. The controller scans CONTROL_CONFIG_PATH for a file corresponding to pattern
"onload-bindername-xxxxx.json". When controller runs within AAAA binder it searches for "onload-audio-xxxx.json". First file found in the path the loaded
any other files corresponding to the same pather are ignored and only generate a warning.

Each bloc in the configuration file are defined with
 * label: must be provided is used either for debugging or as input for the action (eg: signal name, control name, ...)
 * info:  optional used for documentation purpose only

Note by default controller config search path is defined at compilation time, but path might be overloaded with CONTROL_CONFIG_PATH
environment variable. Setenv 'CONTROL_ONLOAD_PROFILE'=xxxx to overload 'onload-default-profile' initialisation sequence.

### Config is organised in 4 sections:

 * metadata
 * onload defines the set of action to be executed at startup time
 * control defines the set of controls with corresponding actions
 * event define the set of actions to be executed when receiving a given signal

### Metadata

As today matadata is only used for documentation purpose.
 * label + version mandatory
 * info optional

### OnLoad section

Defines startup time configuration. Onload may provide multiple initialisation profiles, each with a different label.
 * label is mandatory. Label is used to select onload profile at initialisation through DispatchOneOnLoad("onload-label") API;
 * info is optional
 * plugin provides optional unique plugin name. Plugin should follow "onload-bindername-xxxxx.ctlso" patern
   and are search into CONTROL_PLUGIN_PATH. When defined controller will execute user provided function context=CTLP_ONLOAD(label,version,info).
   The context returned by this routine is provided back to any C routines call later by the controller. Note that Lua2C function
   are prefix in Lua script with plugin label (eg: MyPlug: in following config sample)
 * lua2c list of Lua commands shipped with provided plugin.
 * require list of binding that should be initialised before the controller starts. Note that some listed requirer binding might be absent,
   nevertheless any present binding from this list will be started before controller binding, missing ones generate a warning.
 * action the list of action to execute during loadtime. Any failure in action will prevent controller binding from starting.

### Control section

Defines a list of controls that are accessible through (api="control", verb="request", control="control-label").

 * label mandatory
 * info optional
 * permissions Cynara needed privileges to request this control (same as AppFw-V2)
 * action the list of actions

### Event section

Defines a list of actions to be executed on event reception. Even can do anything a controller can (change state,
send back signal, ...) eg: if a controller subscribes to vehicule speed, then speed-event may ajust master-volume to speed.

 * label mandatory
 * info optional
 * action the list of actions

### Actions Categories

Controler support tree categories of actions. Each action return a status status where 0=success and 1=failure.
 * AppFw API, Provides a generic model to request other bindings. Requested binding can be local (eg: ALSA/UCM) or
   external (eg: vehicle signalling).
    * api  provides requested binding API name
    * verb provides verb to requested binding
    * args optionally provides a jsonc object for targeted binding API. Note that 'args' are statically defined
       in JSON configuration file. Controler client may also provided its own arguments from the query list. Targeted
       binding receives both arguments defined in the config file and the argument provided by controller client.
 * C-API, when defined in the onload section, the plugin may provided C native API with CTLP-CAPI(apiname, label, args, query, context).
   Plugin may also create Lua command with  CTLP-Lua2C(LuaFuncName, label, args, query, context). Where args+query are JSONC object
   and context the value return from CTLP_ONLOAD function. Any missing value is set to NULL.
 * Lua-API, when compiled with Lua option, the controller support action defined directly in Lua script. During "onload" phase the
   controller search in CONTROL_Lua_PATH file with pattern "onload-bindername-xxxx.lua". Any file corresponding to this pattern
   is automatically loaded. Any function defined in those Lua script can be called through a controller action. Lua functions receive
   three parameters (label, args, query).

Note: Lua added functions systematically prefix. AGL standard AppFw functions are prefixed with AGL: (eg: AGL:notice(), AGL_success(), ...).
User Lua functions added though the plugin and CTLP_Lua2C are prefix with plugin label (eg: MyPlug:HelloWorld1).

### Avaliable Application Framework Commands

Each Lua AppFw commands should be prefixed by AFB:

 * AFB:notice ("format", arg1,... argn) LUA table are print directly as json string with '%s'.
   AFB:error, AFB:warning, AFB:info, AFB:debug work on the same model. Printed message are limited to 512 characters.

 * AFB:service ('API', 'VERB', {query}, "Lua_Callback_Name", {context}) asynchronous call to an other binding. When empty query/context should be set to '{}'
   and not to 'nil'. When 'nil' Lua does not send 'NULL' value but remove arguments to calling stack. WARNING:"Callback"
   is the name of the callback as a string and not a pointer to the callback. (If someone as a solution to fix this, please
   let me known). Callback is call as LUA "function Alsa_Get_Hal_CB (error, result, context)" where:
   * error is a Boolean
   * result is the full answer from AppFw (do not forget to extract response)
   * context is a copy of the Lua table pas as argument (warning it's a copy not a pointer to original table)

 * error,result=AFB:servsync('API', 'VERB', {query}) Save as previous but for synchronous call. Note that Lua accept multiple
   return. AFB:servsync return both the error message and the response as a Lua table. Like for AFB:service user should not
   forget to extract response from result.

 * AFB:success(request, response) request is the opaque handle pass when Lua is called from (api="control", verb="docall").
   Response is a Lua table that will be return to client.

 * AFB:fail(request, response) same as for success. Note that LUA generate automatically the error code from Lua function name.
   The response is tranformed to a json string before being return to client.

 * EventHandle=AFB:evtmake("MyEventName") Create an event and return the handle as an opaque handle. Note that due to a limitation
   of json_object this opaque handle cannot be passed as argument in a callback context.

 * AFB:subscribe(request, MyEventHandle) Subscribe a given client to previously created event.

 * AFB:evtpush (MyEventHandle, MyEventData) Push an event to every subscribed client. MyEventData is a Lua table that will be
   send as a json object to corresponding clients.

 * timerHandle=AFB:timerset (MyTimer, "Timer_Test_CB", context) Initialise a timer from MyTimer Lua table. This table should contend 3 elements:
   MyTimer={[l"abel"]="MyTimerName", ["delay"]=timeoutInMs, ["count"]=nBOfCycles}. Note that is count==0 then timer is cycle
   infinitively. Context is a standard Lua table. This function return an opaque handle to be use to further control the timer.

 * AFB:timerclear(timerHandle) Kill an existing timer. Return an error when timer does not exit.

 * MyTimer=AFB:timerget(timerHandle) Return Label, Delay and Count of an active timer. Return an error when timerHandle does not
   point on an active timer.

Note: Except for function call during binding initialisation period. Lua call are protected and should return clean message
  even when improperly used. If you find bug please report.

### Adding Lua command from User Plugin

User Plugin is optional and may provide either native C-action accessible directly from controller actions as defined in
JSON config file, or alternatively may provide at set of Lua commands usable inside any script (onload, control,event). A simple
plugin that provide both natice C API and Lua commands is provided as example (see ctl-plugin-sample.c). Technically a
plugin is a simple sharelibrary and any code fitting in sharelib might be used as a plugin. Developer should nevertheless
not forget that except when no-concurrency flag was at binding construction time, any binding should to be thread safe.

A plugin must be declare with CTLP_REGISTER("MyCtlSamplePlugin"). This entry point defines a special structure that is check
at plugin load time by the controller. Then you have an optional init routine declare with CTLP_ONLOAD(label, version, info).
This init routine receives controller onload profile as selected by DispatchOnLoad("profile"). The init routine may create
a plugin context that is later one presented to every plugin API this for both LUA and native C ones. Then each:

 * C API declare with CTLP_CAPI (MyCFunction, label, argsJ, queryJ, context) {your code}. Where:
     * MyFunction is your function
     * Label is a string containing the name of your function
     * ArgsJ a json_object containing the argument attach the this control in JSON config file.
     * context your C context as return from  CTLP_ONLOAD

 * Lua API declarewith TLP_LUA2C (MyLuaCFunction, label, argsJ, context) {your code}. Where
     * MyLuaCFunction is both the name of your C function and Lua command
     * Label your function name as a string
     * Args the arguments passed this time from Lua script and not from Json config file.
     * Query is not provided as LuaC function are called from a script and not directly from controller action list.

Warning: Lua samples use with controller enforce strict mode. As a result every variables should be declare either as
local or as global. Unfortunately "luac" is not smart enough to handle strict mode at build time and errors only appear
at run time. Because of this strict mode every global variables (which include functions) should be prefix by '_'.
Note that LUA require an initialisation value for every variables and declaring something like "local myvar" wont
allocate "myvar"

### Debugging Facilities

Controler Lua script are check for syntax from CMAKE template with Luac. When needed to go further an developer API allow to
execute directly Lua command within controller context from Rest/Ws (api=control, verb=lua_doscript). DoScript API takes two
other optional arguments func=xxxx where xxxx is the function to execute within Lua script and args a JSON object to provide
input parameter. When funcname is not given by default the controller try to execute middle filename doscript-xxxx-????.lua.

When executed from controller Lua script may use any AppFw Apis as well as any L2C user defined commands in plugin.

### Running as Standalone Controller

Controller is a standard binding and can then be started independently of AAAA. When started with from build repository with
```
afb-daemon --port=1234 --workdir=. --roothttp=../htdocs --tracereq=common --token= --verbose --binding=./Controller-afb/afb-control-afb.so
```

Afb-Daemon only load controller bindings without search for the other binding. In this case the name of the process is not change
to afb-audio and controller binding will search for a configuration file name 'onload-daemon-xxx.json'. This model can be used
to implement for testing purpose or simply to act as the glue in between a UI and other binder/services.

## Config Sample

Here after a simple configuration sample.

```
{
    "$schema": "ToBeDone",
    "metadata": {
        "label": "sample-audio-control",
        "info": "Provide Default Audio Policy for Multimedia, Navigation and Emergency",
        "version": "1.0"
    },
    "onload": [{
            "label": "onload-default",
            "info": "onload initialisation config",
                        "plugin": {
                "label" : "MyPlug",
                "sharelib": "ctl-audio-plugin-sample.ctlso",
                "lua2c": ["Lua2cHelloWorld1", "Lua2cHelloWorld2"]
            },
            "require": ["intel-hda", "jabra-usb", "scarlett-usb"],
            "actions": [
                {
                    "label": "onload-sample-cb",
                    "info": "Call control sharelib install entrypoint",
                    "callback": "SamplePolicyInit",
                    "args": {
                        "arg1": "first_arg",
                        "nextarg": "second arg value"
                    }
                }, {
                    "label": "onload-sample-api",
                    "info": "Assert AlsaCore Presence",
                    "api": "alsacore",
                    "verb": "ping",
                    "args": "test"
                }, {
                    "label": "onload-hal-lua",
                    "info": "Load avaliable HALs",
                    "lua": "Audio_Init_Hal"
                }
            ]
        }],
    "controls":
            [
                {
                    "label": "multimedia",
                    "permissions": "urn:AGL:permission:audio:public:mutimedia",
                    "actions": {
                            "label": "multimedia-control-lua",
                            "info": "Call Lua Script function Test_Lua_Engin",
                            "lua": "Audio_Set_Multimedia"
                        }
                }, {
                    "label": "navigation",
                    "permissions": "urn:AGL:permission:audio:public:navigation",
                    "actions": {
                            "label": "navigation-control-lua",
                            "info": "Call Lua Script to set Navigation",
                            "lua": "Audio_Set_Navigation"
                        }
                }, {
                    "label": "emergency",
                    "permissions": "urn:AGL:permission:audio:public:emergency",
                    "actions": {
                            "label": "emergency-control-ucm",
                            "lua": "Audio_Set_Emergency"
                        }
                }, {
                    "label": "multi-step-sample",
                    "info" : "all actions must succeed for control to be accepted",
                    "actions": [{
                            "label": "multimedia-control-cb",
                            "info": "Call Sharelib Sample Callback",
                            "callback": "sampleControlNavigation",
                            "args": {
                                "arg1": "snoopy",
                                "arg2": "toto"
                            }
                        }, {
                            "label": "navigation-control-ucm",
                            "api": "alsacore",
                            "verb": "ping",
                            "args": {
                                "test": "navigation"
                            }
                        }, {
                            "label": "navigation-control-lua",
                            "info": "Call Lua Script to set Navigation",
                            "lua": "Audio_Set_Navigation"
                        }]
                }
            ],
    "events":
            [
                {
                    "label": "Vehicle-Speed",
                    "info": "Action when Vehicule speed change",
                    "actions": [
                        {
                            "label": "speed-action-1",
                            "callback": "Blink-when-over-130",
                            "args": {
                                "speed": 130
                                "blink-speed": 1000
                            }
                        }, {
                            "label": "Adjust-Volume",
                            "lua": "Adjust_Volume_To_Speed",
                        }
                    ]
                },
                {
                    "label": "Reverse-Engage",
                    "info": "When Reverse Gear is Engage",
                    "actions": [
                        {
                            "label": "Display-Rear-Camera",
                            "callback": "Display-Rear-Camera",
                        }, {
                            "label": "Prevent-Phone-Call",
                            "api"  : "phone",
                            "verb" : "status",
                            "args": {
                                "call-accepted": false
                            }
                        }
                    ]
                },
                {
                    "label": "Neutral-Engage",
                    "info": "When Reverse Neutral is Engage",
                    "actions": [
                        {
                            "label": "Authorize-Video",
                            "api"  : "video",
                            "verb" : "status",
                            "args": {
                                "tv-accepted": true
                            }
                        }
                    ]
                }
            ]
}

```

