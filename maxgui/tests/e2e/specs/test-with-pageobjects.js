/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
////////////////////////////////////////////////////////////////
// For authoring Nightwatch tests, see
// https://nightwatchjs.org/guide
//
// For more information on working with page objects see:
//   https://nightwatchjs.org/guide/working-with-page-objects/
////////////////////////////////////////////////////////////////

module.exports = {
    beforeEach: browser => browser.init(),

    'e2e tests using page objects': browser => {
        const loginpage = browser.page.loginpage()
        loginpage.waitForElementVisible('@appContainer')

        const app = loginpage.section.app
        app.assert.elementCount('@logo', 1)
        app.expect.section('@headline').text.to.match(/^Welcome$/)

        browser.end()
    },
}
