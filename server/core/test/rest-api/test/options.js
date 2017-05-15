require("../utils.js")()

describe("Request Options", function() {
    before(startMaxScale)

    it("pretty=true", function() {
        return request.get(base_url + "/services/?pretty=true")
            .should.eventually.satisfy(validate)
    })

    after(stopMaxScale)
});
