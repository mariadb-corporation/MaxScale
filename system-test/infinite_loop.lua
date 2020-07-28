function createInstance()
end

function newSession(a, b)
end

function closeSession()
end

function routeQuery(sql)
    print("LUA: routeQuery")
    print(sql)
    if (string.find(sql, "LUA_INFINITE_LOOP"))
    then
        while (true)
        do
        end
    end
end

function clientReply()
    print("LUA: clientReply")
end

function diagnostic()
    return "Lua routeQuery will not return if sql = select LUA_INFINITE_LOOP"
end
