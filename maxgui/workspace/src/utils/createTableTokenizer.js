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
import tokens from '@wsSrc/utils/createTableTokens'

const WHITESPACE_OPT = '\\s*'
const WHITESPACE_REQ = '\\s+'
const PAREN_OPEN = '\\('
const PAREN_CLOSE = '\\)'
const ANY_CHARS_REQ = '[\\s\\S]+'
const ESCAPE = '[\'"`]'
const WORD_REQ = '\\w+'
const ESCAPED_NAME = `${ESCAPE}.*?${ESCAPE}`

/**
 * @param {String} param.token - regex token to be grouped
 * @param {Boolean} [param.optional] - optional group. add ? after group
 * @param {Boolean} [param.ignore] - ignore capturing the group. e.g (?:\\w+)?
 * @returns {String} e.g (\\w+)?
 */
function createGroup({ token, optional = false, ignore = false }) {
    return `(${ignore ? '?:' : ''}${token})${optional ? '?' : ''}`
}

function genDefaultValuePattern() {
    const DEFAULT_NUM = '-?\\d+\\.?\\d*' // e.g. 123, 123.000 or -123
    const DEFAULT_EXP = `${PAREN_OPEN}.*?${PAREN_CLOSE}` // e.g. (col1 + col2)
    const DEFAULT_FN =
        WORD_REQ +
        createGroup({
            token: `${PAREN_OPEN}(?:.+?)?${PAREN_CLOSE}`,
            optional: true,
            ignore: true,
        }) // e.g. current_timestamp()

    return [ESCAPED_NAME, DEFAULT_NUM, DEFAULT_EXP, DEFAULT_FN].join('|')
}

// Capturing groups
const TBL_DEFS = [PAREN_OPEN, createGroup({ token: ANY_CHARS_REQ }), PAREN_CLOSE].join(
    WHITESPACE_OPT
)
const TBL_OPTS = createGroup({ token: ANY_CHARS_REQ })

// name + size groups
const DATA_TYPE_DEF =
    createGroup({ token: WORD_REQ }) +
    createGroup({ token: PAREN_OPEN + '(.*?)' + PAREN_CLOSE, optional: true, ignore: true })

const UN = createGroup({ token: tokens.un, optional: true })
const ZF = createGroup({ token: tokens.zf, optional: true })
const NN = createGroup({ token: tokens.nn, optional: true })

const CHARSET = createGroup({
    token: tokens.charset + WHITESPACE_REQ + `(${WORD_REQ})`,
    optional: true,
    ignore: true,
})
const COLLATE = createGroup({
    token: tokens.collate + WHITESPACE_REQ + `(${WORD_REQ})`,
    optional: true,
    ignore: true,
})

const GENERATED = createGroup({
    token: [
        tokens.generated,
        '(?:\\((.*)\\))', //generated expression value
        `(${tokens.virtual}|${tokens.persistent}|${tokens.stored})`, //type of generated
    ].join(WHITESPACE_REQ),
    optional: true,
    ignore: true,
})

const AI = createGroup({ token: tokens.ai, optional: true }) // AUTO_INCREMENT

const DEFAULT = createGroup({
    token: tokens.default + WHITESPACE_REQ + `(${genDefaultValuePattern()})`,
    optional: true,
    ignore: true,
})

const COMMENT = createGroup({
    token: tokens.comment + WHITESPACE_REQ + `(${ESCAPED_NAME})`,
    optional: true,
    ignore: true,
})

export default {
    createTable: new RegExp(
        [tokens.createTable, createGroup({ token: ESCAPED_NAME }), TBL_DEFS, TBL_OPTS].join(
            WHITESPACE_OPT
        ),
        'i'
    ),
    tableOptions: new RegExp(
        [
            createGroup({ token: WORD_REQ }),
            '=',
            createGroup({ token: `${ESCAPE}.*${ESCAPE}|${WORD_REQ}` }),
        ].join(WHITESPACE_OPT),
        'gi'
    ),
    colDef: new RegExp(
        [`(${ESCAPED_NAME})`, DATA_TYPE_DEF].join(WHITESPACE_REQ) +
            WHITESPACE_OPT +
            [UN, ZF, NN, CHARSET, COLLATE, GENERATED, AI, DEFAULT, COMMENT].join(WHITESPACE_OPT) +
            createGroup({ token: `${WHITESPACE_REQ}|$`, ignore: true }),
        'i'
    ),
}
