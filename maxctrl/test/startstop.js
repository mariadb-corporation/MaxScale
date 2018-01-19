require('../test_utils.js')()

describe('Start/Stop Commands', function() {
    before(startMaxScale)

    it('stop service', function() {
        return verifyCommand('stop service Read-Connection-Router', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.attributes.state.should.equal('Stopped')
            })
    })

    it('start service', function() {
        return verifyCommand('start service Read-Connection-Router', 'services/Read-Connection-Router')
            .then(function(res) {
                res.data.attributes.state.should.equal('Started')
            })
    })

    it('stop monitor', function() {
        return verifyCommand('stop monitor MariaDB-Monitor', 'monitors/MariaDB-Monitor')
            .then(function(res) {
                res.data.attributes.state.should.equal('Stopped')
            })
    })

    it('start monitor', function() {
        return verifyCommand('start monitor MariaDB-Monitor', 'monitors/MariaDB-Monitor')
            .then(function(res) {
                res.data.attributes.state.should.equal('Running')
            })
    })

    it('stop maxscale', function() {
        return verifyCommand('stop maxscale', 'services')
            .then(function(res) {
                res.data.forEach((i) => {
                    i.attributes.state.should.equal('Stopped')
                })
            })
    })

    it('start maxscale', function() {
        return verifyCommand('start maxscale', 'services')
            .then(function(res) {
                res.data.forEach((i) => {
                    i.attributes.state.should.equal('Started')
                })
            })
    })

    after(stopMaxScale)
});
