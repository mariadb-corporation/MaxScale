require("../utils.js")()

describe("Module parameters", function() {
    before(startMaxScale)

    it("parameter types are correct", async function() {

        const check_type = function(obj, name, type) {
            obj.data.attributes.parameters.find(e => e.name == name).type.should.equal(type)
        }

        var core = await request.get(base_url + "/maxscale/modules/maxscale")
        check_type(core, "admin_auth", "bool")
        check_type(core, "admin_host", "string")
        check_type(core, "admin_port", "int")
        check_type(core, "auth_connect_timeout", "duration")
        check_type(core, "dump_last_statements", "enum")
        check_type(core, "log_throttling", "throttling")
        check_type(core, "query_classifier_cache_size", "size")
        check_type(core, "rebalance_window", "count")

        var mon = await request.get(base_url + "/maxscale/modules/mariadbmon")
        check_type(mon, "events", "enum_mask")
    });

    after(stopMaxScale)
});
