require("../utils.js")()

describe("Service", function() {
    before(startMaxScale)

    it("change service parameter", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.attributes.parameters.enable_root_user = true
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.attributes.parameters.enable_root_user.should.be.true
            })
    });


    it("remove service relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                delete svc.data.relationships["servers"]
                delete svc.data.relationships["servers"]
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.should.be.empty
            })
    });

    it("add service relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships = {
                    servers: {
                        data: [
                            {id: "server1", type: "servers"},
                            {id: "server2", type: "servers"},
                            {id: "server3", type: "servers"},
                            {id: "server4", type: "servers"},
                        ]
                    }
                }

                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.servers.data[0].id.should.be.equal("server1")
            })
    });

    it("bad request body with `relationships` endpoint should be rejected", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/servers", {json: {data: null}})
            .should.be.rejected
    })

    it("remove service relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/servers", { json: {data: []}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true }))
            .then((res) => {
                res.data.relationships.should.not.have.keys("servers")
            })
    });

    it("add service relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/servers",
                             { json: { data: [
                                 {id: "server1", type: "servers"},
                                 {id: "server2", type: "servers"},
                                 {id: "server3", type: "servers"},
                                 {id: "server4", type: "servers"},
                             ]}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.servers.data.should.have.lengthOf(4)
            })
    });

    const listener = {
        "links": {
            "self": "http://localhost:8989/v1/services/RW-Split-Router/listeners"
        },
        "data": {
            "attributes": {
                "parameters": {
                    "port": 4012,
                    "protocol": "MariaDBClient",
                    "authenticator": "MySQLAuth",
                    "address": "127.0.0.1"
                }
            },
            "id": "RW-Split-Listener-2",
            "type": "listeners"
        }
    }

    it("create a listener", function() {
        return request.post(base_url + "/services/RW-Split-Router/listeners", {json: listener})
            .should.be.fulfilled
    });

    it("create an already existing listener", function() {
        return request.post(base_url + "/services/RW-Split-Router/listeners", {json: listener})
            .should.be.rejected
    });

    it("destroy a listener", function() {
        return request.delete(base_url + "/services/RW-Split-Router/listeners/RW-Split-Listener-2")
            .should.be.fulfilled
    });

    it("destroy a nonexistent listener", function() {
        return request.delete(base_url + "/services/RW-Split-Router/listeners/I-bet-this-listener-exists")
            .should.be.rejected
    });

    it("destroy a static listener", function() {
        return request.delete(base_url + "/services/RW-Split-Router/listeners/RW-Split-Listener")
            .should.be.rejected
    });

    after(stopMaxScale)
});
