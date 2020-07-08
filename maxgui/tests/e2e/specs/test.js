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
// For authoring Nightwatch tests, see
// https://nightwatchjs.org/guide

module.exports = {
    'default e2e tests': browser => {
        browser
            .init()
            .waitForElementVisible('#app')
            .assert.containsText('span', 'MaxScale')
            .assert.elementCount('img', 1)
            .end()
    },
}
