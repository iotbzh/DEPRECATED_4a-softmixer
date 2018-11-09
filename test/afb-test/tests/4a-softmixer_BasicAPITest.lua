--[[
   Copyright (C) 2018 "IoT.bzh"
   Author Frédéric Marec <frederic.marec@iot.bzh>

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

_AFT.setBeforeAll(function()
  if not os.execute("/bin/bash ".._AFT.bindingRootDir.."/var/beforeAll.sh") then
      print("Fail to setup test")
      return -1
  else
      return 0
  end
end)

local testPrefix ="4a-smixer_BasicAPITest_"

-- This tests the ping verb of the audio High level API
_AFT.testVerbStatusSuccess(testPrefix.."ping","smixer","ping",{}, nil, nil)
