/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export const WHITESPACE_ZERO_OR_MORE = '\\s*'
export const PAREN_OPEN = '\\('
export const PAREN_CLOSE = '\\)'
export const ALL_CHARS_OPTIONAL = '[\\s\\S]*'
export const ESCAPE = '[\'"`]'

const TBL_NAME = `${ESCAPE}(.*)${ESCAPE}`

const TBL_DEFS = [PAREN_OPEN, `(${ALL_CHARS_OPTIONAL})`, PAREN_CLOSE].join(WHITESPACE_ZERO_OR_MORE)

const TBL_OPTS = `(${ALL_CHARS_OPTIONAL})`

export default {
    createTable: new RegExp(
        ['CREATE', 'TABLE', TBL_NAME, TBL_DEFS, TBL_OPTS].join(WHITESPACE_ZERO_OR_MORE),
        'i'
    ),
    tableOptions: new RegExp(
        ['(\\w+)', '=', `(${ESCAPE}(.*)${ESCAPE}|(\\w+))`].join(WHITESPACE_ZERO_OR_MORE),
        'gi'
    ),
}
