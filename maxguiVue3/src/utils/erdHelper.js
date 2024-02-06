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
import { CREATE_TBL_TOKENS as tokens, ALL_TABLE_KEY_CATEGORIES } from '@/constants/workspace'
import {
  lodash,
  uuidv1,
  quotingIdentifier as quoting,
  addComma,
  immutableUpdate,
} from '@/utils/helpers'
import { t as typy } from 'typy'
import { RELATIONSHIP_OPTIONALITY } from '@/components/workspace/worksheets/ErdWke/config'
import { integerTypes } from '@/components/common/MxsDdlEditor/utils'

/**
 * @param {object} param
 * @param {Array.<object>} param.cols - parsed index columns to be transformed
 * @param {object} param.col_map - parsed columns to be looked up
 * @returns {Array.<object} transformed cols where the `name` property is replaced with `id`
 */
function replaceColNamesWithIds({ cols, col_map }) {
  return lodash.cloneDeep(cols).map((item) => {
    if (!item.name) return item
    const col = Object.values(col_map).find((c) => c.name === item.name)
    item.id = col.id
    delete item.name
    return item
  })
}

/**
 * Transform parsed keyMap of a category into a data structure used
 * by the DDL editor in ERD worksheet. i.e. the referenced names will be replaced
 * with corresponding target ids found in lookupTables. This is done to ensure the
 * relationships between tables are intact when changing the target names.
 * @param {object} param
 * @param {object} param.keyMap - keyMap
 * @param {object} param.col_map - column map
 * @param {array} [param.lookupTables] - all parsed tables in the ERD. Required when parsing FK
 * @returns {object} transformed key
 */
function replaceNamesWithIds({ keyMap, col_map, lookupTables }) {
  return Object.values(lodash.cloneDeep(keyMap)).reduce((map, key) => {
    // transform referencing cols
    key.cols = replaceColNamesWithIds({ cols: key.cols, col_map })
    if (key.ref_tbl_name) {
      let refTbl
      // Find referenced node
      lookupTables.forEach((tbl) => {
        if (tbl.options.name === key.ref_tbl_name && tbl.options.schema === key.ref_schema_name)
          refTbl = tbl
      })
      // If refTbl is not found, it's not in lookupTables, the fk shouldn't be transformed
      if (refTbl) {
        key.ref_tbl_id = refTbl.id
        // Remove properties that are no longer needed.
        delete key.ref_tbl_name
        delete key.ref_schema_name
        // transform ref_cols
        key.ref_cols = replaceColNamesWithIds({
          cols: key.ref_cols,
          col_map: refTbl.defs.col_map,
        })
      }
    }
    map[key.id] = key
    return map
  }, {})
}

/**
 * @param {object} keyCategoryMap - keyCategoryMap that have been mapped with ids via genDdlEditorData function
 * @returns {object} e.g. { 'col_id': ['PRIMARY KEY', ], }
 */
function genColKeyTypeMap(keyCategoryMap) {
  return Object.keys(keyCategoryMap).reduce((map, category) => {
    const colIds = Object.values(keyCategoryMap[category])
      .map((key) => key.cols.map((c) => c.id))
      .flat()
    colIds.forEach((id) => {
      if (!map[id]) map[id] = []
      map[id].push(category)
    })
    return map
  }, {})
}

function isSingleUQ({ keyCategoryMap, colId }) {
  return Object.values(keyCategoryMap[tokens.uniqueKey] || {}).some((key) =>
    key.cols.every((c) => c.id === colId)
  )
}

/**
 * Transform the parsed output of TableParser into a structure
 * that is used by mxs-ddl-editor.
 * @param {object} param
 * @param {object} param.parsedTable - output of TableParser
 * @param {array} param.lookupTables - parsed tables. Use for transforming FKs
 * @param {object} param.charsetCollationMap - collations mapped by charset
 * @returns {object}
 */
function genDdlEditorData({ parsedTable, lookupTables = [], charsetCollationMap }) {
  const {
    defs: { col_map, key_category_map },
  } = parsedTable

  const charset = parsedTable.options.charset
  const collation =
    typy(parsedTable, 'options.collation').safeString ||
    typy(charsetCollationMap, `[${charset}].defCollation`).safeString

  return {
    ...parsedTable,
    options: {
      ...parsedTable.options,
      charset,
      collation,
    },
    defs: {
      col_map,
      key_category_map: immutableUpdate(key_category_map, {
        $set: ALL_TABLE_KEY_CATEGORIES.reduce((res, type) => {
          if (key_category_map[type])
            res[type] = replaceNamesWithIds({
              keyMap: key_category_map[type],
              col_map,
              lookupTables,
            })
          return res
        }, {}),
      }),
    },
  }
}

/**
 * @param {object} param.nodeData - node data
 * @param {string} param.highlightColor - highlight color
 */
function genErdNode({ nodeData, highlightColor }) {
  return {
    id: nodeData.id,
    data: nodeData,
    styles: { highlightColor },
    x: 0,
    y: 0,
    vx: 0,
    vy: 0,
  }
}

const getNodeHighlightColor = (node) => typy(node, 'styles.highlightColor').safeString

const getColDefData = ({ node, colId }) => node.data.defs.col_map[colId]

const getOptionality = (col) =>
  col.nn ? RELATIONSHIP_OPTIONALITY.MANDATORY : RELATIONSHIP_OPTIONALITY.OPTIONAL

const areIndexed = ({ keyCategoryMap, category, colIds }) => {
  const keys = Object.values(typy(keyCategoryMap[category]).safeObjectOrEmpty)
  if (!keys.length) return false
  return keys.some((key) =>
    lodash.isEqual(
      key.cols.map((c) => c.id),
      colIds
    )
  )
}

function areUniqueCols({ node, colIds }) {
  const keyCategoryMap = node.data.defs.key_category_map
  return (
    areIndexed({ keyCategoryMap, category: tokens.primaryKey, colIds }) ||
    areIndexed({ keyCategoryMap, category: tokens.uniqueKey, colIds })
  )
}
function getCardinality({ node, cols }) {
  return areUniqueCols({ node, colIds: cols.map((c) => c.id) }) ? '1' : 'N'
}

function isColMandatory({ node, colId }) {
  return getOptionality(getColDefData({ node, colId })) === RELATIONSHIP_OPTIONALITY.MANDATORY
}
/**
 * @param {object} param.srcNode - referencing table
 * @param {object} param.targetNode - referenced table
 * @param {object} param.fk - parsed fk data
 * @param {string} param.colId - source column id
 * @param {string} param.refColId - target column id
 * @param {boolean} param.isPartOfCompositeKey - is a part of composite FK
 * @param {string} param.srcCardinality - either 1 or N
 * @param {string} param.targetCardinality - either 1 or N
 */
function genErdLink({
  srcNode,
  targetNode,
  fk,
  colId,
  refColId,
  isPartOfCompositeKey,
  srcCardinality,
  targetCardinality,
}) {
  const { id, name, on_delete, on_update } = fk

  const colData = getColDefData({ node: srcNode, colId })
  const referencedColData = getColDefData({ node: targetNode, colId: refColId })
  if (!colData || !referencedColData) return null

  const srcOptionality = getOptionality(colData)
  const targetOptionality = getOptionality(referencedColData)
  const type = `${srcOptionality}..${srcCardinality}:${targetOptionality}..${targetCardinality}`

  let link = {
    id, // use fk id as link id
    source: srcNode.id,
    target: targetNode.id,
    relationshipData: {
      type,
      name,
      on_delete,
      on_update,
      src_attr_id: colId,
      target_attr_id: refColId,
    },
  }
  if (isPartOfCompositeKey) link.isPartOfCompositeKey = isPartOfCompositeKey
  return link
}
/**
 *
 * @param {object} param
 * @param {object} param.node - node has the FK
 * @param {string} param.colId - id of the FK column
 * @param {string} param.refCold - id of the referenced column
 * @param {object} param.colKeyCategoryMap
 * @returns {boolean}
 */
function isIdentifyingRelation({ node, colId, refColId, colKeyCategoryMap }) {
  const pk = typy(node, `data.defs.key_category_map[${tokens.primaryKey}]`).safeObjectOrEmpty
  const pkCols = typy(Object.values(pk), '[0].cols').safeArray
  const hasCompositePk = pkCols.length > 1
  let isIdentify = false
  if (hasCompositePk) {
    const colKeyTypes = colKeyCategoryMap[colId]
    const refColKeyTypes = colKeyCategoryMap[refColId]
    if (colKeyTypes.includes(tokens.primaryKey) && refColKeyTypes.includes(tokens.primaryKey))
      isIdentify = true
  }
  return isIdentify
}
/**
 *
 * @param {object} param.srcNode - source node
 * @param {object} param.fk - foreign key object
 * @param {array} param.nodes - all nodes of the ERD
 * @param {boolean} param.isAttrToAttr - isAttrToAttr: FK is drawn to associated column
 * @param {object} param.colKeyCategoryMap
 * @returns {object}
 */
function handleGenErdLink({ srcNode, fk, nodes, isAttrToAttr, colKeyCategoryMap }) {
  const { cols, ref_tbl_id, ref_cols } = fk
  let links = []

  const target = ref_tbl_id
  const targetNode = nodes.find((n) => n.id === target)
  const invisibleHighlightColor = getNodeHighlightColor(targetNode)

  if (targetNode) {
    const srcCardinality = getCardinality({ node: srcNode, cols })
    const targetCardinality = getCardinality({
      node: targetNode,
      cols: ref_cols,
    })
    for (const [i, item] of cols.entries()) {
      const colId = item.id
      const refColId = typy(ref_cols, `[${i}].id`).safeString

      let linkObj = genErdLink({
        srcNode,
        targetNode,
        fk,
        colId,
        refColId,
        isPartOfCompositeKey: i >= 1,
        srcCardinality,
        targetCardinality,
      })
      if (linkObj) {
        if (linkObj.isPartOfCompositeKey) linkObj.hidden = !isAttrToAttr
        linkObj.styles = { invisibleHighlightColor }
        if (isIdentifyingRelation({ node: srcNode, colId, refColId, colKeyCategoryMap }))
          linkObj.styles.dashArr = 0
        links.push(linkObj)
      }
    }
  }
  return links
}

/**
 *
 * @param {array} param.links - all links of an ERD
 * @param {object} param.node - node
 * @returns {array} links of the provided node
 */
function getNodeLinks({ links, node }) {
  return links.filter(
    (d) =>
      /**
       * d3 auto map source/target object in links, but
       * persisted links in IndexedDB stores only the id
       */
      lodash.get(d.source, 'id', d.source) === node.id ||
      lodash.get(d.target, 'id', d.target) === node.id
  )
}

/**
 *
 * @param {array} param.links - all links of an ERD
 * @param {object} param.node - node
 * @returns {array} links that are not connected to the provided node
 */
function getExcludedLinks({ links, node }) {
  return links.filter((link) => !getNodeLinks({ links, node }).includes(link))
}

/**
 *
 * @param {Array} tables - parsed tables
 * @returns {Object.<string, Object.<string, string>>} e.g. { "tbl_1": { "col_1": "id", "col_2": "name" } }
 */
function createTablesColNameMap(tables) {
  return tables.reduce((res, tbl) => {
    res[tbl.id] = Object.values(tbl.defs.col_map).reduce((map, col) => {
      map[col.id] = col.name
      return map
    }, {})
    return res
  }, {})
}

/**
 *
 * @param {Array} tables - parsed tables
 * @returns {Array}
 */
function genRefTargets(tables) {
  return tables.map((tbl) => {
    const schema = typy(tbl, 'options.schema').safeString
    const name = typy(tbl, 'options.name').safeString
    return {
      id: tbl.id,
      text: `${quoting(schema)}.${quoting(name)}`,
    }
  })
}

/**
 * @param {object} param
 * @param {object} param.key - FK object
 * @param {object} param.refTargetMap - referenced target map
 * @param {object} param.tablesColNameMap - column names of all tables in ERD mapped by column id
 * @param {object} param.stagingColNameMap - column names of the table has the FK mapped by column id
 * @returns {string} FK constraint
 */
function genConstraint({ key, refTargetMap, tablesColNameMap, stagingColNameMap }) {
  const {
    name,
    cols,
    ref_tbl_id,
    ref_schema_name = '',
    ref_tbl_name = '',
    ref_cols,
    on_delete,
    on_update,
  } = key

  const constraintName = quoting(name)

  const colNames = cols.map(({ id }) => quoting(stagingColNameMap[id])).join(addComma())

  const refTarget = ref_tbl_id
    ? typy(refTargetMap[ref_tbl_id], 'text').safeString
    : `${quoting(ref_schema_name)}.${quoting(ref_tbl_name)}`

  const refColNames = ref_cols
    .map(({ id, name }) => {
      if (id) {
        const colName = typy(tablesColNameMap, `[${ref_tbl_id}][${id}]`).safeString
        return quoting(colName)
      }
      return quoting(name)
    })
    .join(addComma())

  return [
    `${tokens.constraint} ${constraintName}`,
    `${tokens.foreignKey} (${colNames})`,
    `${tokens.references} ${refTarget} (${refColNames})`,
    `${tokens.on} ${tokens.delete} ${on_delete}`,
    `${tokens.on} ${tokens.update} ${on_update}`,
  ].join('\n')
}

/**
 * @param {string} param.colName - column name
 * @param {string} param.category - key category
 * @returns {string} key name
 */
const genKeyName = ({ colName, category }) => `${colName}_${category.replace(/\s/, '_')}`

/**
 * @param {object} param
 * @param {object} param.defs - parsed defs
 * @param {string} param.category
 * @param {string} param.colId
 * @returns {object} new key object
 */
function genKey({ defs, category, colId }) {
  const col = defs.col_map[colId]
  const colName = col.name
  return {
    id: `key_${uuidv1()}`,
    cols: [{ id: colId }],
    name: genKeyName({ colName, category }),
  }
}
/**
 *
 * @param {string} str - a string with type and size combined e.g. INT(11)
 * @returns {string} type e.g. INT
 */
function extractType(typeAndSize) {
  let str = typeAndSize
  if (str.includes('(')) return str.slice(0, str.indexOf('('))
  return str
}
/**
 *
 * The foreign key columns and the referenced columns must be of the same type, or similar types.
 * For integer types, the size and sign must also be the same.
 * https://mariadb.com/kb/en/foreign-keys/
 * @param {object} param
 * @param {object} param.src
 * @param {object} param.target
 * @param {string} param.colId
 * @param {string} param.targetColId
 * @returns {boolean}
 */
function validateFkColTypes({ src, target, colId, targetColId }) {
  const col = getColDefData({ node: src, colId })
  const targetCol = getColDefData({ node: target, colId: targetColId })

  const typeAndSize = col.data_type.toUpperCase()
  const targetTypeAndSize = targetCol.data_type.toUpperCase()

  const type = extractType(typeAndSize)
  const targetType = extractType(targetTypeAndSize)
  if (type === targetType) {
    // For integer types, the size and sign must also be the same.
    if (integerTypes.includes(type)) {
      return typeAndSize === targetTypeAndSize && col.un === targetCol.un
    }
    return true
  }
  /**
   * Handle similar types is not trivial as it involves several checks.
   * For now, the users is responsible for that case. So this function returns
   * true when both types are different,
   */
  return true
}

function genIdxColOpts({ tableColMap, disableHandler = () => false }) {
  return Object.values(tableColMap).reduce((options, c) => {
    const type = c.data_type
    options.push({
      id: c.id,
      text: c.name,
      type,
      disabled: typy(disableHandler).safeFunction(type),
    })
    return options
  }, [])
}

export default {
  genColKeyTypeMap,
  isSingleUQ,
  genDdlEditorData,
  genErdNode,
  areUniqueCols,
  isColMandatory,
  handleGenErdLink,
  getNodeLinks,
  getExcludedLinks,
  createTablesColNameMap,
  genRefTargets,
  genConstraint,
  genKey,
  genKeyName,
  validateFkColTypes,
  genIdxColOpts,
}
