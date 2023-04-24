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

const NOT_SINGLE_QUOTE = "[^']*"
const NOT_DBL_QUOTE = '[^"]*'
const NOT_BACKTICK = '[^`]*'
/**
 *
 * @param {Boolean} isMultiple - If true, match `col_a`, otherwise match `col_a`
 * @returns {String}
 */
function getEscapeStrReg(isMultiple) {
    return [
        createGroup({
            token: `'${NOT_SINGLE_QUOTE}(?:''${NOT_SINGLE_QUOTE})*'` + (isMultiple ? ',\\s*' : ''),
            ignore: true,
        }),
        createGroup({
            token: `"${NOT_DBL_QUOTE}(?:""${NOT_DBL_QUOTE})*"` + (isMultiple ? ',\\s*' : ''),
            ignore: true,
        }),
        createGroup({
            token: `\`${NOT_BACKTICK}(?:\`\`${NOT_BACKTICK})*\`` + (isMultiple ? ',\\s*' : ''),
            ignore: true,
        }),
    ].join('|')
}

/**
 * @param {String} param.name - named capturing group for the string.
 * @param {Boolean} param.isMultiple - captures escaped string separated by a comma. e.g. `col_a`,`col_b`
 * @returns {String} regex for capturing: `t1` or `my test``s table`, ...
 */
function createEscapeStrGroup({ name, isMultiple }) {
    let escapedStrGroup = `(?:${getEscapeStrReg(isMultiple)})`
    if (isMultiple) escapedStrGroup += '*' + `(?:${getEscapeStrReg()})`
    return `(?<${name}>${escapedStrGroup})`
}
/**
 * @param {String} param.token - regex token to be grouped
 * @param {String} [param.name] - named group, ignore param must be false
 * @param {Boolean} [param.optional] - optional group. add ? after group
 * @param {Boolean} [param.ignore] - ignore capturing the group. e.g (?:\\w+)?
 * @returns {String} e.g (\\w+)?
 */
function createGroup({ token, name = '', optional = false, ignore = false }) {
    let res = '('
    if (ignore) res += '?:'
    else if (name) res += `?<${name}>`
    res += `${token})`
    if (optional) res += '?'
    return res
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

    return [
        createEscapeStrGroup({ name: 'default_exp_name_escape' }),
        DEFAULT_NUM,
        DEFAULT_EXP,
        DEFAULT_FN,
    ].join('|')
}

const COL_NAME = createEscapeStrGroup({ name: 'col_name' })
const DATA_TYPE = createGroup({ token: WORD_REQ, name: 'data_type' })
const DATA_TYPE_SIZE = createGroup({
    token: PAREN_OPEN + createGroup({ token: '.*?', name: 'data_type_size' }) + PAREN_CLOSE,
    optional: true,
    ignore: true,
})

const UN = createGroup({ token: tokens.un, name: 'un', optional: true })
const ZF = createGroup({ token: tokens.zf, name: 'zf', optional: true })
const NN = createGroup({ token: tokens.nn, name: 'nn', optional: true })

const CHARSET = createGroup({
    token: tokens.charset + WHITESPACE_REQ + createGroup({ token: WORD_REQ, name: 'charset' }),
    optional: true,
    ignore: true,
})
const COLLATE = createGroup({
    token: tokens.collate + WHITESPACE_REQ + createGroup({ token: WORD_REQ, name: 'collate' }),
    optional: true,
    ignore: true,
})

const GENERATED = createGroup({
    token: [
        tokens.generated,
        createGroup({
            token: PAREN_OPEN + createGroup({ token: '.*', name: 'generated_exp' }) + PAREN_CLOSE,
            ignore: true,
        }),
        createGroup({
            token: `${tokens.virtual}|${tokens.persistent}|${tokens.stored}`,
            name: 'generated_type',
        }),
    ].join(WHITESPACE_REQ),
    optional: true,
    ignore: true,
})

const AI = createGroup({ token: tokens.ai, optional: true, name: 'ai' }) // AUTO_INCREMENT

const DEFAULT = createGroup({
    token:
        tokens.default +
        WHITESPACE_REQ +
        createGroup({ token: genDefaultValuePattern(), name: 'default_exp' }),
    optional: true,
    ignore: true,
})

const COMMENT = createGroup({
    token: tokens.comment + WHITESPACE_REQ + createEscapeStrGroup({ name: 'comment' }),
    optional: true,
    ignore: true,
})

const NON_FK_TOKENS = [tokens.primary, tokens.unique, tokens.fullText, tokens.spatial]

export default {
    createTable: new RegExp(
        [
            tokens.createTable,
            createEscapeStrGroup({ name: 'table_name' }),
            [
                PAREN_OPEN,
                createGroup({ token: ANY_CHARS_REQ, name: 'table_definitions' }),
                PAREN_CLOSE,
            ].join(WHITESPACE_OPT),
            createGroup({ token: ANY_CHARS_REQ, name: 'table_options' }),
        ].join(WHITESPACE_OPT),
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
        [COL_NAME, DATA_TYPE + DATA_TYPE_SIZE].join(WHITESPACE_REQ) +
            WHITESPACE_OPT +
            [UN, ZF, NN, CHARSET, COLLATE, GENERATED, AI, DEFAULT, COMMENT].join(WHITESPACE_OPT) +
            createGroup({ token: `${WHITESPACE_REQ}|$`, ignore: true }),
        'i'
    ),
    nonFks: new RegExp(
        '^' +
            [
                createGroup({
                    token: createGroup({ token: NON_FK_TOKENS.join('|'), name: 'category' }),
                    optional: true,
                }),
                tokens.key,
                // index_name (optional as PK doesn't have name)
                createGroup({
                    token: createEscapeStrGroup({ name: 'name' }),
                    optional: true,
                }),
                // index_col_name group
                createGroup({
                    token:
                        PAREN_OPEN +
                        createGroup({
                            token: createEscapeStrGroup({
                                name: 'escaped_str_group',
                                isMultiple: true,
                            }),
                            name: 'col_names',
                        }) +
                        PAREN_CLOSE,
                }),
            ].join(WHITESPACE_OPT),
        'i'
    ),
}
