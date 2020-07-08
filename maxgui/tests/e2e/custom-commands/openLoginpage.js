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
/**
 * A basic Nightwatch custom command
 *  which demonstrates usage of ES6 async/await instead of using callbacks.
 *  The command name is the filename and the exported "command" function is the command.
 *
 * Example usage:
 *   browser.openHomepage();
 *
 * For more information on writing custom commands see:
 *   https://nightwatchjs.org/guide/extending-nightwatch/#writing-custom-commands
 *
 */
module.exports = {
    command: async function() {
        // Other Nightwatch commands are available via "this"
        // .init() simply calls .url() command with the value of the "launch_url" setting
        this.init()
        this.waitForElementVisible('#app')

        const result = await this.elements('css selector', '#app')
        console.log('result.value', result.value)
        this.assert.strictEqual(result.value.length, 1)
    },
}
