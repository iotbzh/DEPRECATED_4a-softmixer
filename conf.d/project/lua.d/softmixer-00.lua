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

  Note: this file should be called before any other to assert declare function
  is loaded before anything else.

  References:
    http://lua-users.org/wiki/DetectingUndefinedVariables

--]]


--===================================================
--=  Niklas Frykholm
-- basically if user tries to create global variable
-- the system will not let them!!
-- call GLOBAL_lock(_G)
--
--===================================================
function GLOBAL_lock(t)
  local mt = getmetatable(t) or {}
  mt.__newindex = lock_new_index
  setmetatable(t, mt)
end

--===================================================
-- call GLOBAL_unlock(_G)
-- to change things back to normal.
--===================================================
function GLOBAL_unlock(t)
  local mt = getmetatable(t) or {}
  mt.__newindex = unlock_new_index
  setmetatable(t, mt)
end

function lock_new_index(t, k, v)
  if (string.sub(k,1,1) ~= "_") then
    GLOBAL_unlock(_G)
    error("GLOBALS are locked -- " .. k ..
          " must be declared local or prefix with '_' for globals.", 2)
  else
    rawset(t, k, v)
  end
end

function unlock_new_index(t, k, v)
  rawset(t, k, v)
end

-- return serialised version of printable table
function Dump_Table(o)
   if type(o) == 'table' then
      local s = '{ '
      for k,v in pairs(o) do
         if type(k) ~= 'number' then k = '"'..k..'"' end
         s = s .. '['..k..'] = ' .. Dump_Table(v) .. ','
      end
      return s .. '} '
   else
      return tostring(o)
   end
end


-- simulate C prinf function
printf = function(s,...)
    io.write(s:format(...))
    io.write("\n")
    return
end

-- lock global variable
GLOBAL_lock(_G)
