require("../utils.js")()


describe("HTTP Headers", function() {
    before(startMaxScale)

    it("ETag changes after modification", function() {
        return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
            .then(function(resp) {
                resp.headers.etag.should.be.equal("0")
                var srv = JSON.parse(resp.body)
                delete srv.data.relationships
                return request.put(base_url + "/servers/server1", {json: srv})
            })
            .then(function() {
                return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
            })
            .then(function(resp) {
                resp.headers.etag.should.be.equal("1")
            })
    });

    it("Last-Modified changes after modification", function() {
        var date;

        return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
            .then(function(resp) {

                // Store the current modification time
                resp.headers["last-modified"].should.not.be.null
                date = resp.headers["last-modified"]

                // Modify resource after three seconds
                setTimeout(function() {
                    var srv = JSON.parse(resp.body)

                    srv.data.relationships = {
                        services: {
                            data: [
                                {id: "RW-Split-Router", type: "services"}
                            ]
                        }
                    }

                    request.put(base_url + "/servers/server1", {json: srv})
                        .then(function() {
                            return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
                        })
                        .then(function(resp) {
                            resp.headers["last-modified"].should.not.be.null
                            resp.headers["last-modified"].should.not.be.equal(date)
                        })

                }, 3000)
            })
    });

    after(stopMaxScale)
});
