local skynet = require "skynet"
local deque = require "deque"
require "skynet.manager"
local mode = ...

local timestr = tostring(math.floor(skynet.time()))
math.randomseed(tonumber(timestr:sub(1,6)))

if mode == "slave" then

skynet.start(function()
	skynet.dispatch("lua", function (session, source, obj)
		if session == 0 then
			skynet.abort()
			return
		end
		local obj = deque.clone(obj)
		skynet.ret()

		while true do
			skynet.sleep(math.random(2))
			print("read:", obj:pop())
		end
	end)
end)

else

skynet.start(function()
	local slave = skynet.newservice(SERVICE_NAME, "slave")

	local obj, addr = deque.new(100)

	skynet.call(slave, "lua", addr)

	for i=1,100 do
		skynet.sleep(math.random(2))
		obj:push(i)
		print("write", i, obj:size())
	end

	skynet.send(slave, "lua", "close")
end)

end