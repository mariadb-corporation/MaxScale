require("../utils.js")()
const WebSocket = require('ws');

describe("Logs", function() {
    before(startMaxScale)

    it("change logging options", function() {
        return request.get(base_url + "/maxscale/logs")
            .then(function(resp) {
                var logs = JSON.parse(resp)
                logs.data.attributes.parameters.maxlog.should.be.true
                logs.data.attributes.parameters.syslog.should.be.true
                logs.data.attributes.parameters.ms_timestamp.should.be.false
                logs.data.attributes.parameters.maxlog = false
                logs.data.attributes.parameters.syslog = false
                logs.data.attributes.parameters.ms_timestamp = true
                logs.data.attributes.parameters.log_throttling.count = 1
                logs.data.attributes.parameters.log_throttling.suppress = 1
                logs.data.attributes.parameters.log_throttling.window = 1

                return request.patch(base_url + "/maxscale/logs", {json: logs})
            })
            .then(function(resp) {
                return request.get(base_url + "/maxscale/logs")
            })
            .then(function(resp) {
                var logs = JSON.parse(resp)
                logs.data.attributes.parameters.maxlog.should.be.false
                logs.data.attributes.parameters.syslog.should.be.false
                logs.data.attributes.parameters.ms_timestamp.should.be.true
                logs.data.attributes.parameters.log_throttling.count.should.be.equal(1)
                logs.data.attributes.parameters.log_throttling.suppress.should.be.equal(1)
                logs.data.attributes.parameters.log_throttling.window.should.be.equal(1)
            })
    });

    it("flush logs", function() {
        return request.post(base_url + "/maxscale/logs/flush")
            .should.be.fulfilled
    })

    after(stopMaxScale)
});

function checkLog(log) {
    for (l of log) {
        l.message.should.not.be.empty
        l.timestamp.should.not.be.empty
        l.priority.should.not.be.empty
    }
}

describe("Log Data", function() {
    before(startMaxScale)

    it("returns log data", async function() {
        var res = await request.get(base_url + "/maxscale/logs/data", {json: true})
        res.data.attributes.log.should.not.be.empty
    });

    it("returns 50 rows of data by default", async function() {
        var res = await request.get(base_url + "/maxscale/logs/data", {json: true})
        res.data.attributes.log.length.should.equal(50)
    });

    it("paginates logs", async function() {
        var page = await request.get(base_url + "/maxscale/logs/data?page[size]=1", {json: true})
        page.data.attributes.log.length.should.equal(1)
    });

    it("has working pagination links", async function() {
        var page1 = await request.get(base_url + "/maxscale/logs/data?page[size]=1", {json: true})
        page1.data.attributes.log.length.should.equal(1)

        var page2 = await request.get(page1.links.prev, {json: true, auth: {user: 'admin', password: 'mariadb'}})
        page2.data.attributes.log.length.should.equal(1)
        page2.data.attributes.log[0].should.not.deep.equal(page1.data.attributes.log[0])
    });

    it("maxlog parsing works", async function() {
        var data = { data: { attributes: { parameters: { maxlog: true, syslog: false }}}}
        await request.patch(base_url + "/maxscale", {json: data})

        var res = await request.get(base_url + "/maxscale/logs/data", {json: true})
        res.data.attributes.log_source.should.equal("maxlog")
        res.data.attributes.log.should.not.be.empty
        checkLog(res.data.attributes.log)

        data.data.attributes.parameters = { maxlog: true, syslog: true }
        await request.patch(base_url + "/maxscale", {json: data})
    });

    it("syslog parsing works", async function() {
        var data = { data: { attributes: { parameters: { maxlog: false, syslog: true }}}}
        await request.patch(base_url + "/maxscale", {json: data})

        var res = await request.get(base_url + "/maxscale/logs/data", {json: true})
        res.data.attributes.log_source.should.equal("syslog")
        res.data.attributes.log.should.not.be.empty
        checkLog(res.data.attributes.log)

        data.data.attributes.parameters = { maxlog: true, syslog: true }
        await request.patch(base_url + "/maxscale", {json: data})
    });

    it("log is not parsed without maxlog or syslog", async function() {
        var data = { data: { attributes: { parameters: { maxlog: false, syslog: false }}}}
        await request.patch(base_url + "/maxscale", {json: data})

        var res = await request.get(base_url + "/maxscale/logs/data", {json: true})
        expect(res.data.attributes.log_source).to.be.undefined
        expect(res.data.attributes.log).to.be.undefined

        data.data.attributes.parameters = { maxlog: true, syslog: true }
        await request.patch(base_url + "/maxscale", {json: data})
    });

    after(stopMaxScale)
});

function connectWebSocket() {
    return new Promise((resolve, reject) => {
        const ws_url = "ws://" + credentials + "@" + host + "/maxscale/logs/stream"
        const ws = new WebSocket(ws_url, 'ws');

        ws.on('open', function() {
            resolve(true)
        });

        ws.on('error', function open(err) {
            reject(err)
        });
    })
}

function openWebSocket(cursor) {
    return new Promise((resolve, reject) => {
        var ws_url = "ws://" + credentials + "@" + host + "/maxscale/logs/stream"

        if (cursor) {
            ws_url += "?page[cursor]=" + cursor
        }

        const ws = new WebSocket(ws_url, 'ws');

        ws.on('message', function(msg) {
            resolve(JSON.parse(msg))
        });

        ws.on('error', function open(err) {
            reject(err)
        });
    })
}

async function testWebSocket() {
    var p = openWebSocket()

    // This will cause at least one message to be logged
    var data = { data: { attributes: { parameters: { log_info: true }}}}
    await request.patch(base_url + "/maxscale", {json: data})
    data.data.attributes.parameters = { log_info: false }
    await request.patch(base_url + "/maxscale", {json: data})

    var res = await p
    expect(res.message).to.not.be.empty
    expect(res.timestamp).to.not.be.empty
    expect(res.priority).to.not.be.empty

    var log = await request.get(base_url + "/maxscale/logs/data", {json: true})

    res = await openWebSocket(log.data.attributes.log[0].id)
    expect(res.message).to.not.be.empty
    expect(res.timestamp).to.not.be.empty
    expect(res.priority).to.not.be.empty
}

describe("Log Streaming", function() {
    before(startMaxScale)

    it("opens WebSocket", async function(){
        connectWebSocket().should.be.fulfilled
    });

    it("streams maxlog data", async function() {
        var data = { data: { attributes: { parameters: { maxlog: true, syslog: false }}}}
        await request.patch(base_url + "/maxscale", {json: data})
        await testWebSocket()
    });

    it("streams syslog data", async function() {
        var data = { data: { attributes: { parameters: { maxlog: false, syslog: true }}}}
        await request.patch(base_url + "/maxscale", {json: data})
        await testWebSocket()
    });

    it("streaming fails when maxlog and syslog are disabled", async function() {
        var data = { data: { attributes: { parameters: { maxlog: false, syslog: false }}}}
        await request.patch(base_url + "/maxscale", {json: data})

        connectWebSocket().should.be.rejected

        data.data.attributes.parameters = { maxlog: true, syslog: true }
        await request.patch(base_url + "/maxscale", {json: data})
    });

    after(stopMaxScale)
});
