/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const sinonChai = require('sinon-chai')
global.sinon = require('sinon')
global.chai = require('chai')
global.expect = chai.expect
global.should = chai.should()
chai.use(sinonChai)
// Required for Vuetify (Create div with a data-app attribute)
const app = document.createElement('div')
app.setAttribute('data-app', 'true')
document.body.appendChild(app)

global.requestAnimationFrame = () => null
global.cancelAnimationFrame = () => null

// global define
// this prevents console from being printed out
console.error = () => {}
console.info = () => {}
console.warn = () => {}

window.HTMLElement.prototype.scrollIntoView = () => {}
// mock $helpers.copyTextToClipboard as execCommand is undefined in jsdom
global.document.execCommand = () => {}
global.AbortController = sinon.stub()

process.on('uncaughtException', error => {
    // Ignore "Uncaught error outside test suite" message
    if (error.message.includes('No available storage method found.')) return
    console.error(error)
})
process.on('unhandledRejection', error => {
    // Ignore "Uncaught error outside test suite" message
    if (error.message.includes('No available storage method found.')) return
    console.error(error)
})
