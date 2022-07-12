function createInstance()
end

function newSession(a, b)
end

function closeSession()
end

function routeQuery(sql)
    if (string.find(sql, "LUA_INFINITE_LOOP"))
    then
        print("Starting infinite loop")
        while (true)
        do
        end
    end
end

function clientReply()
end

function diagnostic()
    return "Lua routeQuery will not return if sql = select LUA_INFINITE_LOOP"
end
