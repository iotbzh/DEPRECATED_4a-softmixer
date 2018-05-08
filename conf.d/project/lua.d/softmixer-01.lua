--[[
  Copyright (C) 2016 "IoT.bzh"
  Author Fulup Ar Foll <fulup@iot.bzh>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.


  NOTE: strict mode: every global variables should be prefixed by '_'
--]]

-- Static variables should be prefixed with _
_EventHandle={}

-- Call when AlsaCore return HAL active list
function _AlsaPingCB_ (source, result, context)

    AFB:notice (source, "--InLua-- PingCB: result='%s'", Dump_Table(result))

end


-- Display receive arguments and echo them to caller
function _mixer_config_ (source, args)

    local devin = {
        ["path"]= "/dev/snd/by-path/platform-snd_aloop.0",
        ["dev"]= 1,
        ["sub"]= 0,
        ["numid"]= 51,
    }

    local devout = {
        ["path"]= "/dev/snd/by-id/usb-YAMAHA_Corporation_YAMAHA_AP-U70_USB_Audio_00-00",
        ["dev"]= 0,
        ["sub"]= 0,
    }

    local params = {
        ["rate"]= 44100,
        ["channels"]= 2,
    }

    -- Call AlsaSoftRouter
    L2C:alsarouter(source, {["devin"]= devin, ["devout"]= devout, ["params"]= params})

    AFB:notice (source, "--InLua-- _mixer_config_ done")

    return 0 -- happy end
end

-- Display receive arguments and echo them to caller
function _init_softmixer_ (source, args)

    -- create event to push change audio roles to potential listeners
    _EventHandle=AFB:evtmake(source, "control")

    _mixer_config_ (source, args)

end
