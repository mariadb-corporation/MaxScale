/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import {
    CREATE_TBL_TOKENS as tokens,
    ALL_TABLE_KEY_TYPES,
    COL_ATTRS,
    COL_ATTR_IDX_MAP,
    GENERATED_TYPES,
} from '@wsSrc/store/config'
import { lodash, immutableUpdate, uuidv1 } from '@share/utils/helpers'
import { t as typy } from 'typy'
import { quotingIdentifier as quoting, addComma } from '@wsSrc/utils/helpers'
import { RELATIONSHIP_OPTIONALITY } from '@wsSrc/components/worksheets/ErdWke/config'
import { checkCharsetSupport, integerTypes } from '@wsSrc/components/common/MxsDdlEditor/utils'

/**
 * @param {object} param
 * @param {Array.<object>} param.cols - parsed index columns to be transformed
 * @param {Array.<Array>} param.lookupCols - parsed columns to be looked up
 * @returns {Array.<object} transformed cols where the `name` property is replaced with `id`
 */
function replaceColNamesWithIds({ cols, lookupCols }) {
    return lodash.cloneDeep(cols).map(item => {
        if (!item.name) return item
        const col = lookupCols.find(c => c.name === item.name)
        item.id = col.id
        delete item.name
        return item
    })
}

/**
 * Transform parsed keys of a table into a data structure used
 * by the DDL editor in ERD worksheet. i.e. the referenced names will be replaced
 * with corresponding target ids found in lookupTables. This is done to ensure the
 * relationships between tables are intact when changing the target names.
 * @param {object} param
 * @param {array} param.keys - keys
 * @param {array} param.cols - all columns of a table
 * @param {array} [param.lookupTables] - all parsed tables in the ERD. Required when parsing FK
 * @returns {array} new array
 */
function replaceNamesWithIds({ keys, cols, lookupTables }) {
    return lodash.cloneDeep(keys).map(key => {
        // transform referencing cols
        key.cols = replaceColNamesWithIds({ cols: key.cols, lookupCols: cols })
        if (key.ref_tbl_name) {
            let refTbl
            // Find referenced node
            lookupTables.forEach(tbl => {
                if (tbl.name === key.ref_tbl_name && tbl.options.schema === key.ref_schema_name)
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
                    lookupCols: refTbl.definitions.cols,
                })
            }
        }
        return key
    })
}

/**
 * @param {object} keys - keys that have been mapped with ids via tableParserTransformer function
 * @returns {object} e.g. { 'col_id': ['PRIMARY KEY', ], }
 */
function genColKeyTypeMap(keys) {
    return Object.keys(keys).reduce((map, type) => {
        const colIds = keys[type].map(key => key.cols.map(c => c.id)).flat()
        colIds.forEach(id => {
            if (!map[id]) map[id] = []
            map[id].push(type)
        })
        return map
    }, {})
}

function isSingleUQ({ keys, colId }) {
    return typy(keys, `[${tokens.uniqueKey}]`).safeArray.some(key =>
        key.cols.every(c => c.id === colId)
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
function tableParserTransformer({ parsedTable, lookupTables = [], charsetCollationMap }) {
    const {
        definitions: { cols, keys },
    } = parsedTable
    const transformedKeys = immutableUpdate(keys, {
        $set: ALL_TABLE_KEY_TYPES.reduce((res, type) => {
            if (keys[type])
                res[type] = replaceNamesWithIds({
                    keys: keys[type],
                    cols,
                    lookupTables,
                })
            return res
        }, {}),
    })
    const charset = parsedTable.options.charset
    const collation =
        typy(parsedTable, 'options.collation').safeString ||
        typy(charsetCollationMap, `[${charset}].defCollation`).safeString
    const {
        ID,
        NAME,
        TYPE,
        PK,
        NN,
        UN,
        UQ,
        ZF,
        AI,
        GENERATED_TYPE,
        DEF_EXP,
        CHARSET,
        COLLATE,
        COMMENT,
    } = COL_ATTRS
    const colKeyTypeMap = genColKeyTypeMap(transformedKeys)
    const transformedCols = cols.map(col => {
        let type = col.data_type
        if (col.data_type_size) type += `(${col.data_type_size})`
        const keyTypes = colKeyTypeMap[col.id] || []

        let uq = false
        if (keyTypes.includes(tokens.uniqueKey)) {
            /**
             * UQ input is a checkbox for a column, so it can't handle composite unique
             * key. Thus ignoring composite unique key.
             */
            uq = isSingleUQ({ keys: transformedKeys, colId: col.id })
        }
        return {
            [ID]: col.id,
            [NAME]: col.name,
            [TYPE]: type.toUpperCase(),
            [PK]: keyTypes.includes(tokens.primaryKey),
            [NN]: col.is_nn,
            [UN]: col.is_un,
            [UQ]: uq,
            [ZF]: col.is_zf,
            [AI]: col.is_ai,
            [GENERATED_TYPE]: col.generated_type ? col.generated_type : GENERATED_TYPES.NONE,
            [DEF_EXP]: col.generated_exp ? col.generated_exp : typy(col.default_exp).safeString,
            [CHARSET]: checkCharsetSupport(col.data_type) ? col.charset || charset : '',
            [COLLATE]: checkCharsetSupport(col.data_type) ? col.collate || collation : '',
            [COMMENT]: typy(col.comment).safeString,
        }
    })
    return {
        id: parsedTable.id,
        options: {
            ...parsedTable.options,
            charset,
            collation,
            name: parsedTable.name,
        },
        definitions: {
            cols: transformedCols.map(col => [...Object.values(col)]),
            keys: transformedKeys,
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

const getNodeHighlightColor = node => typy(node, 'styles.highlightColor').safeString

const getColDefData = ({ node, colId }) =>
    node.data.definitions.cols.find(col => col[COL_ATTR_IDX_MAP[COL_ATTRS.ID]] === colId)

const getOptionality = colData =>
    colData[COL_ATTR_IDX_MAP[COL_ATTRS.NN]]
        ? RELATIONSHIP_OPTIONALITY.MANDATORY
        : RELATIONSHIP_OPTIONALITY.OPTIONAL

const isIndex = ({ indexDefs, cols }) => indexDefs.some(def => lodash.isEqual(def.cols, cols))

function isUniqueCol({ node, cols }) {
    const keys = node.data.definitions.keys
    const pks = keys[tokens.primaryKey] || []
    const uniqueKeys = keys[tokens.uniqueKey] || []
    if (!pks.length && !uniqueKeys.length) return false
    return isIndex({ indexDefs: pks, cols }) || isIndex({ indexDefs: uniqueKeys, cols })
}
function getCardinality(params) {
    return isUniqueCol(params) ? '1' : 'N'
}

/**
 * @param {object} param.srcNode - referencing table
 * @param {object} param.targetNode - referenced table
 * @param {object} param.fk - parsed fk data
 * @param {string} param.indexColName - source column name
 * @param {string} param.referencedIndexColName - target column name
 * @param {boolean} param.isPartOfCompositeKey - is a part of composite FK
 * @param {string} param.srcCardinality - either 1 or N
 * @param {string} param.targetCardinality - either 1 or N
 */
function genErdLink({
    srcNode,
    targetNode,
    fk,
    indexColId,
    refColId,
    isPartOfCompositeKey,
    srcCardinality,
    targetCardinality,
}) {
    const { id, name, on_delete, on_update } = fk

    const colData = getColDefData({ node: srcNode, colId: indexColId })
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
            src_attr_id: indexColId,
            target_attr_id: refColId,
        },
    }
    if (isPartOfCompositeKey) link.isPartOfCompositeKey = isPartOfCompositeKey
    return link
}

/**
 *
 * @param {object} param.srcNode - source node
 * @param {object} param.fk - foreign key object
 * @param {array} param.nodes - all nodes of the ERD
 * @param {boolean} param.isAttrToAttr - isAttrToAttr: FK is drawn to associated column
 * @returns
 */
function handleGenErdLink({ srcNode, fk, nodes, isAttrToAttr }) {
    const { cols, ref_tbl_id, ref_cols } = fk
    let links = []

    const target = ref_tbl_id
    const targetNode = nodes.find(n => !n.hidden && n.id === target)
    const invisibleHighlightColor = getNodeHighlightColor(targetNode)

    if (targetNode) {
        const srcCardinality = getCardinality({ node: srcNode, cols })
        const targetCardinality = getCardinality({
            node: targetNode,
            cols: ref_cols,
        })
        for (const [i, item] of cols.entries()) {
            const indexColId = item.id
            const refColId = typy(ref_cols, `[${i}].id`).safeString
            let linkObj = genErdLink({
                srcNode,
                targetNode,
                fk,
                indexColId,
                refColId,
                isPartOfCompositeKey: i >= 1,
                srcCardinality,
                targetCardinality,
            })
            if (linkObj) {
                if (linkObj.isPartOfCompositeKey) linkObj.hidden = !isAttrToAttr
                linkObj.styles = { invisibleHighlightColor }
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
        d =>
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
    return links.filter(link => !getNodeLinks({ links, node }).includes(link))
}

/**
 *
 * @param {Array} tables - parsed tables
 * @returns {Object.<string, Object.<string, string>>} e.g. { "tbl_1": { "col_1": "id", "col_2": "name" } }
 */
function createTablesColNameMap(tables) {
    const idxOfId = COL_ATTR_IDX_MAP[COL_ATTRS.ID]
    const idxOfName = COL_ATTR_IDX_MAP[COL_ATTRS.NAME]
    return tables.reduce((res, tbl) => {
        res[tbl.id] = typy(tbl, 'definitions.cols').safeArray.reduce((map, arr) => {
            map[arr[idxOfId]] = arr[idxOfName]
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
    return tables.map(tbl => {
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
 * @param {object} param.definitions - parsed definitions
 * @param {string} param.category
 * @param {string} param.colId
 * @returns {object} new key object
 */
function genKey({ definitions, category, colId }) {
    const idxOfId = COL_ATTR_IDX_MAP[COL_ATTRS.ID]
    const idxOfName = COL_ATTR_IDX_MAP[COL_ATTRS.NAME]
    const col = definitions.cols.find(c => c[idxOfId] === colId)
    const colName = col[idxOfName]
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
    const idxOfType = COL_ATTR_IDX_MAP[COL_ATTRS.TYPE]
    const col = getColDefData({ node: src, colId })
    const targetCol = getColDefData({ node: target, colId: targetColId })

    const typeAndSize = col[idxOfType].toUpperCase()
    const targetTypeAndSize = targetCol[idxOfType].toUpperCase()

    const type = extractType(typeAndSize)
    const targetType = extractType(targetTypeAndSize)
    if (type === targetType) {
        // For integer types, the size and sign must also be the same.
        if (integerTypes.includes(type)) {
            const idxOfUN = COL_ATTR_IDX_MAP[COL_ATTRS.UN]
            return typeAndSize === targetTypeAndSize && col[idxOfUN] === targetCol[idxOfUN]
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

export default {
    genColKeyTypeMap,
    isSingleUQ,
    tableParserTransformer,
    genErdNode,
    handleGenErdLink,
    getNodeLinks,
    getExcludedLinks,
    createTablesColNameMap,
    genRefTargets,
    genConstraint,
    genKey,
    genKeyName,
    validateFkColTypes,
}
