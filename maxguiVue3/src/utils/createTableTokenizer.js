/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { CREATE_TBL_TOKENS as tokens } from '@/constants/workspace'

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
const DIGIT_REQ = '\\d+'
const ORDER = 'ASC|DESC'

const INDEX_LENGTH = createGroup({
  token: PAREN_OPEN + DIGIT_REQ + PAREN_CLOSE,
  ignore: true,
  optional: true,
})
const INDEX_ORDER = createGroup({
  token: ORDER,
  ignore: true,
  optional: true,
})
// [(length)] [ASC | DESC]
const INDEX_LENGTH_ORDER_REG = [INDEX_LENGTH, INDEX_ORDER].join(WHITESPACE_OPT)

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

/**
 * @returns {String}
 */
function getEscapeStrReg() {
  return [
    createGroup({
      token: `'${NOT_SINGLE_QUOTE}(?:''${NOT_SINGLE_QUOTE})*'`,
      ignore: true,
    }),
    createGroup({
      token: `"${NOT_DBL_QUOTE}(?:""${NOT_DBL_QUOTE})*"`,
      ignore: true,
    }),
    createGroup({
      token: `\`${NOT_BACKTICK}(?:\`\`${NOT_BACKTICK})*\``,
      ignore: true,
    }),
  ].join('|')
}

/**
 * @param {String} name - named capturing group for the string.
 * @returns {String} regex for capturing: `t1` or `my test``s table`, ...
 */
function createEscapeStrGroup(name) {
  return createGroup({ token: getEscapeStrReg(), name })
}

/**
 *
 * @param {String} action - delete or update
 * @returns {String} reg for ON DELETE | ON UPDATE reference_option
 */
function createOnActionToken(action) {
  return createGroup({
    token: [
      tokens.on,
      tokens[action],
      createGroup({
        token: [tokens.restrict, tokens.cascade, tokens.setNull, tokens.noAction].join('|'),
        name: `on_${action}`,
      }),
    ].join(WHITESPACE_REQ),
    optional: true,
  })
}

/**
 * @param {String} name - name of the group
 * @returns {String} reg for capturing (index_col_name,...)
 */
function createIdxColNamesReg(name) {
  return (
    PAREN_OPEN +
    createGroup({
      token:
        createGroup({
          token: getEscapeStrReg() + INDEX_LENGTH_ORDER_REG + ',\\s*',
          ignore: true,
        }) +
        '*' +
        createGroup({
          token: getEscapeStrReg() + INDEX_LENGTH_ORDER_REG,
          ignore: true,
        }),
      name,
    }) +
    PAREN_CLOSE
  )
}

function wrapExp(reg, flag) {
  return new RegExp('^' + reg + createGroup({ token: `${WHITESPACE_REQ}|$`, ignore: true }), flag)
}

// ========== Column definition groups ==========
const COL_NAME = createEscapeStrGroup('col_name')
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
      name: 'generated',
    }),
  ].join(WHITESPACE_REQ),
  optional: true,
  ignore: true,
})

const AI = createGroup({ token: tokens.ai, optional: true, name: 'ai' }) // AUTO_INCREMENT

// === default value patterns ===
const DEFAULT_NUM = '-?\\d+\\.?\\d*' // e.g. 123, 123.000 or -123
const DEFAULT_EXP = `${PAREN_OPEN}.*?${PAREN_CLOSE}` // e.g. (col1 + col2)
const DEFAULT_FN =
  WORD_REQ +
  createGroup({
    token: `${PAREN_OPEN}(?:.+?)?${PAREN_CLOSE}`,
    optional: true,
    ignore: true,
  }) // e.g. current_timestamp()

const DEFAULT_VALUE_PATTERNS = [
  createEscapeStrGroup('default_exp_name_escape'),
  DEFAULT_NUM,
  DEFAULT_EXP,
  DEFAULT_FN,
].join('|')
const DEFAULT = createGroup({
  token:
    tokens.default +
    WHITESPACE_REQ +
    createGroup({ token: DEFAULT_VALUE_PATTERNS, name: 'default_exp' }),
  optional: true,
  ignore: true,
})

const COMMENT = createGroup({
  token: tokens.comment + WHITESPACE_REQ + createEscapeStrGroup('comment'),
  optional: true,
  ignore: true,
})

// ========== Index definition groups ==========
const INDEX_NAME = createEscapeStrGroup('name')
// optional to handle also plain index
const NON_FKS_CATEGORY = createGroup({
  token: createGroup({
    token: [
      tokens.primaryKey,
      tokens.uniqueKey,
      tokens.fullTextKey,
      tokens.spatialKey,
      tokens.key,
    ].join('|'),
    name: 'category',
  }),
  optional: true,
})
const FK_CATEGORY = createGroup({ token: tokens.foreignKey, name: 'category' })
const COL_NAMES = createIdxColNamesReg('col_names')

// === Reference definitions groups ===
const REF_TARGET = createGroup({
  token:
    createGroup({
      token: createEscapeStrGroup('ref_schema_name') + '.',
      optional: true,
      ignore: true,
    }) + createEscapeStrGroup('ref_tbl_name'),
  ignore: true,
})
const REF_COL_NAMES = createIdxColNamesReg('ref_col_names')

const ON_DELETE = createOnActionToken('delete')
const ON_UPDATE = createOnActionToken('update')

export default {
  createTable: wrapExp(
    [
      tokens.createTable,
      createEscapeStrGroup('table_name'),
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
  colDef: wrapExp(
    [COL_NAME, DATA_TYPE + DATA_TYPE_SIZE].join(WHITESPACE_REQ) +
      WHITESPACE_OPT +
      [UN, ZF, NN, CHARSET, COLLATE, GENERATED, AI, DEFAULT, COMMENT].join(WHITESPACE_OPT),
    'i'
  ),
  nonFks: wrapExp(
    [
      NON_FKS_CATEGORY,
      // index_name (optional as PK doesn't have name)
      createGroup({ token: INDEX_NAME, optional: true }),
      // index_col_name group
      COL_NAMES,
      createGroup({
        token: `COMMENT ${createEscapeStrGroup('comment')}`,
        optional: true,
      }),
    ].join(WHITESPACE_OPT),
    'i'
  ),
  fks: wrapExp(
    createGroup({
      token:
        [
          tokens.constraint,
          INDEX_NAME,
          FK_CATEGORY,
          COL_NAMES,
          tokens.references,
          REF_TARGET,
          REF_COL_NAMES,
        ].join(WHITESPACE_REQ) +
        WHITESPACE_OPT +
        [ON_DELETE, ON_UPDATE].join(WHITESPACE_OPT),
    }),
    'i'
  ),
  indexColNames: new RegExp(
    [
      createGroup({ token: getEscapeStrReg(), name: 'name' }),
      createGroup({
        token: PAREN_OPEN + createGroup({ token: DIGIT_REQ, name: 'length' }) + PAREN_CLOSE,
        ignore: true,
        optional: true,
      }),
      createGroup({ token: ORDER, name: 'order', optional: true }),
    ].join(WHITESPACE_OPT),
    'g'
  ),
}
