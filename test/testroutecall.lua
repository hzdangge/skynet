local skynet = require "skynet"

local mode = ...

if mode == "slave1" then

skynet.start(function()
	skynet.dispatch("lua", function (session, source, target, ...)
		print(session, source, target)
		skynet.redirect(target, source, "lua", session, skynet.pack(...))
	end)
end)

elseif mode == "slave2" then

skynet.start(function()
	skynet.dispatch("lua", function (session, source, ...)
		print(session, source)
		skynet.ret()
	end)
end)

else

skynet.start(function()
	local slave1 = skynet.newservice(SERVICE_NAME, "slave1")
	local slave2 = skynet.newservice(SERVICE_NAME, "slave2")
	skynet.call(slave1, "lua", slave2, "hello", "world")
	print("success")
end)

end