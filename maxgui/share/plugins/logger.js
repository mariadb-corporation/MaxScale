/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/* eslint-disable no-console */
const stackParser = require('stacktrace-parser')
const traceCaller = level => stackParser.parse(new Error().stack)[level]
const defCaller = { file: 'n/a', lineNumber: 'n/a' }
const logger = name => {
    if (name === null || name === undefined)
        throw new Error('You must provide name when calling new logger method')
    return {
        info: (...args) => {
            const caller = traceCaller(2) || defCaller
            const style = 'color: white; background-color: green;'

            console.info(
                '%c INFO ',
                style,
                `[${name}]`,
                `[${caller.file}:${caller.lineNumber}]`,
                ...args
            )
        },
        warn: (...args) => {
            const caller = traceCaller(2) || defCaller
            const style = 'color: black; background-color: yellow;'

            console.warn(
                '%c WARNING ',
                style,
                `[${name}]`,
                `[${caller.file}:${caller.lineNumber}]`,
                ...args
            )
        },
        error: (...args) => {
            const caller = traceCaller(2) || defCaller
            const style = 'color: white; background-color: red;'

            console.error(
                '%c ERROR ',
                style,
                `[${name}]`,
                `[${caller.file}:${caller.lineNumber}]`,
                ...args
            )
        },
    }
}

export default {
    install: Vue => {
        Vue.prototype.$logger = logger
    },
}
