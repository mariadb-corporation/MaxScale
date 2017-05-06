require("../utils.js")()

describe("Service", function() {
    before(startMaxScale)

    it("change service parameter", function() {
        return request.get(base_url + "/services/RW-Split-Router")
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.attributes.parameters.enable_root_user = true
                return request.put(base_url + "/services/RW-Split-Router", {json: svc})
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
                delete svc.data.relationships
                return request.put(base_url + "/services/RW-Split-Router", {json: svc})
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

                return request.put(base_url + "/services/RW-Split-Router", {json: svc})
            })
            .then(function(resp) {
                return request.get(base_url + "/services/RW-Split-Router")
            })
            .then(function(resp) {
                var svc = JSON.parse(resp)
                svc.data.relationships.servers.data[0].id.should.be.equal("server1")
            })
    });

    after(stopMaxScale)
});
