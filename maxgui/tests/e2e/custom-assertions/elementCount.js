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
 * A custom Nightwatch assertion. The assertion name is the filename.
 *
 * Example usage:
 *   browser.assert.elementCount(selector, count)
 *
 * For more information on custom assertions see:
 *   https://nightwatchjs.org/guide/extending-nightwatch/#writing-custom-assertions
 *
 *
 * @param {string|object} selectorOrObject
 * @param {number} count
 */

exports.assertion = function elementCount(selectorOrObject, count) {
    let selector

    // when called from a page object element or section
    if (typeof selectorOrObject === 'object' && selectorOrObject.selector) {
        // eslint-disable-next-line prefer-destructuring
        selector = selectorOrObject.selector
    } else {
        selector = selectorOrObject
    }

    this.message = `Testing if element <${selector}> has count: ${count}`
    this.expected = count
    this.pass = val => val === count
    this.value = res => res.value
    function evaluator(_selector) {
        return document.querySelectorAll(_selector).length
    }
    this.command = cb => this.api.execute(evaluator, [selector], cb)
}
