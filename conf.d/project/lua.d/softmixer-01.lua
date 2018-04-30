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
function _init_softmixer_ (source, args)

    -- create event to push change audio roles to potential listeners
    _EventHandle=AFB:evtmake(source, "control")

    -- get list of supported HAL devices
    AFB:service(source, "alsacore","ping", {}, "_AlsaPingCB_", {})

    -- test Lua2C plugin
    L2C:alsadmix(source, {})

    AFB:notice (source, "--InLua-- _init_softmixer_ done")

    return 0 -- happy end
end
