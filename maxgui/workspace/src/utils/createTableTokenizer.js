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

const WHITESPACE_ZERO_OR_MORE = '\\s*'
const WHITESPACE_ONE_OR_MORE = '\\s+'
const PAREN_OPEN = '\\('
const PAREN_CLOSE = '\\)'
const ANY_CHARS_ZERO_OR_MORE = '[\\s\\S]*'
const ESCAPE = '[\'"`]'

const ESCAPE_NAME = `${ESCAPE}.*?${ESCAPE}`

const TBL_DEFS = [PAREN_OPEN, `(${ANY_CHARS_ZERO_OR_MORE})`, PAREN_CLOSE].join(
    WHITESPACE_ZERO_OR_MORE
)
const TBL_OPTS = `(${ANY_CHARS_ZERO_OR_MORE})`

const DATA_TYPE_DEF = '(\\w+)' + '(?:\\((.*?)\\))?' // name + size

// matching by groups
const UN = '(UNSIGNED)?'
const ZF = '(ZEROFILL)?'
const NN = '(NOT NULL)?'
const CHARSET = '(?:CHARACTER SET\\s+(\\w*))?'
const COLLATE = '(?:COLLATE\\s(\\w*))?'

// group 1: generated expression value, group 2: type of generated
const GENERATED =
    '(?:GENERATED ALWAYS AS' +
    WHITESPACE_ONE_OR_MORE +
    '(?:\\((.*)\\))' +
    WHITESPACE_ONE_OR_MORE +
    '(VIRTUAL|PERSISTENT|STORED))?'

const AI = '(AUTO_INCREMENT)?'

export default {
    createTable: new RegExp(
        ['CREATE', 'TABLE', `(${ESCAPE_NAME})`, TBL_DEFS, TBL_OPTS].join(WHITESPACE_ZERO_OR_MORE),
        'i'
    ),
    tableOptions: new RegExp(
        ['(\\w+)', '=', `(${ESCAPE}.*${ESCAPE}|\\w+)`].join(WHITESPACE_ZERO_OR_MORE),
        'gi'
    ),
    colDef: new RegExp(
        [`(${ESCAPE_NAME})`, DATA_TYPE_DEF].join(WHITESPACE_ONE_OR_MORE) +
            WHITESPACE_ZERO_OR_MORE +
            [UN, ZF, NN, CHARSET, COLLATE, GENERATED, AI].join(WHITESPACE_ZERO_OR_MORE),
        'i'
    ),
}
