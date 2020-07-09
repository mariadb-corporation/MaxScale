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
 * A Nightwatch page object. The page object name is the filename.
 *
 * Example usage:
 *   browser.page.homepage.navigate()
 *
 * For more information on working with page objects see:
 *   https://nightwatchjs.org/guide/working-with-page-objects/
 *
 */

module.exports = {
    url: '/',
    commands: [],

    // A page object can have elements
    elements: {
        appContainer: '#app',
    },

    // Or a page objects can also have sections
    sections: {
        app: {
            selector: '#app',

            elements: {
                logo: 'img',
            },

            // - a page object section can also have sub-sections
            // - elements or sub-sections located here are retrieved using the "app" section as the base
            sections: {
                headline: {
                    selector: 'h1',
                },
            },
        },
    },
}
