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

    it("create a listener", function() {
        var listener = {
            "links": {
                "self": "http://localhost:8989/v1/services/RW-Split-Router/listeners"
            },
            "data": {
                "attributes": {
                    "parameters": {
                        "port": 4012,
                        "protocol": "MySQLClient",
                        "authenticator": "MySQLAuth",
                        "address": "127.0.0.1"
                    }
                },
                "id": "RW-Split-Listener-2",
                "type": "listeners"
            }
        }

        return request.post(base_url + "/services/RW-Split-Router/listeners", {json: listener})
            .should.be.fulfilled
    });

    after(stopMaxScale)
});
