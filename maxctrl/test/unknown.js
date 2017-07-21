require('../test_utils.js')()

describe("Unknown Commands", function() {
    before(startMaxScale)

    var endpoints = [
        'list',
        'show',
        'enable',
        'disable',
        'create',
        'destroy',
        'link',
        'unlink',
        'start',
        'stop',
        'alter',
        'rotate',
        'call',
    ]

    endpoints.forEach(function (i) {
        it('unknown ' + i + ' command', function() {
            return doCommand(i + ' something')
                .should.be.rejected
        })
    })

    after(stopMaxScale)
});
