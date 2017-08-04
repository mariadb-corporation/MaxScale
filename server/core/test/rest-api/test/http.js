require("../utils.js")()


describe("HTTP Headers", function() {
    before(startMaxScale)

    it("ETag changes after modification", function() {
        return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
            .then(function(resp) {
                resp.headers.etag.should.be.equal("\"0\"")
                var srv = JSON.parse(resp.body)
                delete srv.data.relationships
                return request.patch(base_url + "/servers/server1", {json: srv})
            })
            .then(function() {
                return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
            })
            .then(function(resp) {
                resp.headers.etag.should.be.equal("\"1\"")
            })
            .then(function() {
                return request.get(base_url + "/servers", {resolveWithFullResponse: true})
            })
            .then(function(resp) {
                resp.headers.etag.should.be.equal("\"1\"")
            })
    });

    it("Last-Modified changes after modification", function(done) {
        var date;

        request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
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

                    request.patch(base_url + "/servers/server1", {json: srv})
                        .then(function() {
                            return request.get(base_url + "/servers/server1", {resolveWithFullResponse: true})
                        })
                        .then(function(resp) {
                            resp.headers["last-modified"].should.not.be.null
                            resp.headers["last-modified"].should.not.be.equal(date)
                        })
                        .then(function() {
                            return request.get(base_url + "/servers", {resolveWithFullResponse: true})
                        })
                        .then(function(resp) {
                            resp.headers["last-modified"].should.not.be.null
                            resp.headers["last-modified"].should.not.be.equal(date)
                            done()
                        })
                        .catch(function(e) {
                            done(e)
                        })

                }, 2000)
            })
            .catch(function(e) {
                done(e)
            })
    });

    var oldtime = new Date(new Date().getTime() - 1000000).toUTCString();
    var newtime = new Date(new Date().getTime() + 1000000).toUTCString();

    it("request with older If-Modified-Since value", function() {
        return request.get(base_url + "/servers/server1", {
            headers: { "If-Modified-Since":  oldtime}
        }).should.be.fulfilled
    })

    it("request with newer If-Modified-Since value", function() {
        return request.get(base_url + "/servers/server1", {
            resolveWithFullResponse: true,
            headers: { "If-Modified-Since": newtime }
        }).should.be.rejected
    })

    it("request with older If-Unmodified-Since value", function() {
        return request.get(base_url + "/servers/server1", {
            headers: { "If-Unmodified-Since":  oldtime}
        }).should.be.rejected
    })

    it("request with newer If-Unmodified-Since value", function() {
        return request.get(base_url + "/servers/server1", {
            headers: { "If-Unmodified-Since":  newtime}
        }).should.be.fulfilled
    })

    it("request with mismatching If-Match value", function() {
        return request.get(base_url + "/servers/server1", {
            headers: { "If-Match":  "\"0\""}
        }).should.be.rejected
    })

    it("request with matching If-Match value", function() {
        return request.get(base_url + "/servers/server1", { resolveWithFullResponse: true })
            .then(function(resp) {
                return request.get(base_url + "/servers/server1", {
                    headers: { "If-Match":  resp.headers["etag"]}
                })
            })
            .should.be.fulfilled
    })

    it("request with mismatching If-None-Match value", function() {
        return request.get(base_url + "/servers/server1", {
            headers: { "If-None-Match":  "\"0\""}
        }).should.be.fulfilled
    })

    it("request with matching If-None-Match value", function() {
        return request.get(base_url + "/servers/server1", { resolveWithFullResponse: true })
            .then(function(resp) {
                return request.get(base_url + "/servers/server1", {
                    headers: { "If-None-Match":  resp.headers["etag"]}
                })
            })
            .should.be.rejected
    })

    after(stopMaxScale)
});
