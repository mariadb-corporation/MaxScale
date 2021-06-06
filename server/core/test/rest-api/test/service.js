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


    it("missing relationships are not removed", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                delete svc.data.relationships["servers"]
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.should.not.be.empty
            })
    });

    it("remove service relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.servers.data = null
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.should.have.keys("listeners")
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

    it("add service→service relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)

                svc.data.relationships.services = {
                    data: [ {id: "SchemaRouter-Router", type: "services"} ]
                }

                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.services.data[0].id.should.be.equal("SchemaRouter-Router")
            })
    });

    it("remove service→service relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.services.data = null
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.services.data.should.be.empty
            })
    });

    it("add service→monitor relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships = {monitors: {data: [{id: "MariaDB-Monitor", type: "monitors"}]}, servers: {data: null}, services: {data: null}}
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(() => request.get(base_url + "/services/RW-Split-Router"))
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.monitors.data[0].id.should.be.equal("MariaDB-Monitor")
            })
    });

    it("remove service→monitor relationship", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.monitors.data = null
                return request.patch(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.should.not.have.keys("monitors")
            })
    });

    it("bad request body with `relationships` endpoint should be rejected", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/servers", {json: {servers: null}})
            .should.be.rejected
    })

    it("remove service→server relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/servers", { json: {data: null}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true }))
            .then((res) => {
                res.data.relationships.should.not.have.keys("servers")
            })
    });

    it("add service→server relationship via `relationships` endpoint", function() {
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

    it("add service→filter relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/filters",
                             { json: { data: [
                                 {id: "QLA", type: "filters"},
                             ]}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.filters.data.should.have.lengthOf(1)
            })
    });

    it("add service→service relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/services",
                             { json: { data: [
                                 {id: "SchemaRouter-Router", type: "services"},
                             ]}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.services.data.should.have.lengthOf(1)
            })
    });

    it("remove service→filter relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/filters",
                             { json: { data: null}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.should.not.have.keys("filters")
            })
    });

    it("remove service→service relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/services",
                             { json: { data: null}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.services.data.should.be.empty
            })
    });

    it("adding service→monitor relationship via `relationships` endpoint should be rejected when other targets are in use", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/monitors",
                             { json: {data: [{id: "MariaDB-Monitor", type: "monitors"}]}})
            .should.be.rejected
    });

    it("add service→monitor relationship via `relationships` endpoint", function() {
        var body = { data: {relationships:{services: {data:null}, servers: {data:null}, monitors: {data:null}}}}
        return request.patch(base_url + "/services/RW-Split-Router/", {json: body})
            .then(() => request.patch(base_url + "/services/RW-Split-Router/relationships/monitors",
                                      { json: {data: [{id: "MariaDB-Monitor", type: "monitors"}]}}))
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.monitors.data[0].id.should.be.equal("MariaDB-Monitor")
            })
    });

    it("remove service→monitor relationship via `relationships` endpoint", function() {
        return request.patch(base_url + "/services/RW-Split-Router/relationships/monitors",
                             { json: { data: null}})
            .then(() => request.get(base_url + "/services/RW-Split-Router", { json: true}))
            .then((res) => {
                res.data.relationships.should.not.have.keys("monitors")
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

    it("does not destroy a listener of a different service ", function() {
        return request.delete(base_url + "/services/Read-Connection-Router/listeners/RW-Split-Listener-2")
            .should.be.rejected
    });

    it("does not return a listener of a different service ", function() {
        return request.get(base_url + "/services/Read-Connection-Router/listeners/RW-Split-Listener-2")
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
            .should.be.fulfilled
    });

    it("reload users", function() {
        return request.post(base_url + "/services/RW-Split-Router/reload")
            .should.be.fulfilled
    });

    after(stopMaxScale)
});
