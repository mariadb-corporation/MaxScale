require("../utils.js")()


describe("Errors", function()
{
    before(startMaxScale)

    it("error on invalid PATCH request", function()
    {
        return request.patch(base_url + "/servers/server1", { json: {this_is: "a test"}})
               .should.be.rejected
    })

    it("error on invalid POST request", function()
    {
        return request.post(base_url + "/servers", { json: {this_is: "a test"}})
               .should.be.rejected
    })

    after(stopMaxScale)
});
