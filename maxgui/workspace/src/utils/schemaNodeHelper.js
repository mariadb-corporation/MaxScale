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
import {
    NODE_TYPES,
    NODE_GROUP_TYPES,
    NODE_GROUP_CHILD_TYPES,
    NODE_NAME_KEYS,
    SYS_SCHEMAS,
} from '@wsSrc/store/config'
import { lodash } from '@share/utils/helpers'
import { t as typy } from 'typy'
import { map2dArr, quotingIdentifier as quoting } from '@wsSrc/utils/helpers'

/**
 * @returns {String} node key
 */
const genNodeKey = () => lodash.uniqueId('node_key_')

/**
 * @param {Object} param.nodeGroup - A node group. (NODE_GROUP_TYPES). Undefined if param.type === SCHEMA
 * @param {Object} param.data - data of node
 * @param {String} param.type - type of node to be generated
 * @param {String} param.name - name of the node
 * @param {Boolean} [param.nodeAttrs.isLeaf] -If it's true, child nodes are leaf nodes
 * @param {Boolean} [param.nodeAttrs.isEmptyChildren] - generate node with empty children. i.e. node.children = []
 * @returns {Object}  A node in schema sidebar
 */
function genNode({
    nodeGroup,
    data,
    type,
    name,
    nodeAttrs = { isLeaf: false, isEmptyChildren: false },
}) {
    const { SCHEMA, TBL, VIEW, SP, FN, TRIGGER, COL, IDX } = NODE_TYPES
    const { TBL_G, VIEW_G, SP_G, FN_G, COL_G, IDX_G, TRIGGER_G } = NODE_GROUP_TYPES
    const schemaName = type === SCHEMA ? name : getSchemaName(nodeGroup)
    let node = {
        id: type === SCHEMA ? name : `${nodeGroup.id}.${name}`,
        parentNameData:
            type === SCHEMA ? { [type]: name } : { ...nodeGroup.parentNameData, [type]: name },
        key: genNodeKey(),
        type,
        name,
        draggable: true,
        data,
        isSys: SYS_SCHEMAS.includes(schemaName.toLowerCase()),
    }
    /**
     * index name can be duplicated. e.g.composite indexes.
     * So this adds -node.key as a suffix to make sure id is unique.
     */
    if (type === IDX) node.id = `${nodeGroup.id}.${name}-${node.key}`

    node.level = Object.keys(node.parentNameData).length
    //TODO: Rename qualified_name to qualifiedName as others properties are using camelCase
    switch (type) {
        case TBL:
        case VIEW:
        case SP:
        case FN:
            node.qualified_name = `${quoting(schemaName)}.${quoting(node.name)}`
            break
        case TRIGGER:
        case COL:
        case IDX:
            node.qualified_name = `${quoting(getTblName(nodeGroup))}.${quoting(node.name)}`
            break
        case SCHEMA:
            node.qualified_name = quoting(node.name)
            break
    }
    // Auto assign child node groups unless nodeAttrs is provided with values other than the default ones
    switch (type) {
        case VIEW:
        case TBL:
        case SCHEMA: {
            let childTypes = []
            if (type === VIEW || type === TBL) {
                // only TBL node has IDX_G and TRIGGER_G
                childTypes = type === VIEW ? [COL_G] : [COL_G, IDX_G, TRIGGER_G]
            } else childTypes = [TBL_G, VIEW_G, SP_G, FN_G]

            if (!nodeAttrs.isLeaf)
                node.children = childTypes.map(t => genNodeGroup({ parentNode: node, type: t }))
            if (nodeAttrs.isEmptyChildren) node.children = []
            break
        }
    }

    return node
}

/**
 * @param {Object} node
 * @returns {String} database name
 */
const getSchemaName = node => node.parentNameData[NODE_TYPES.SCHEMA]

/**
 * @param {Object} node
 * @returns {String} table name
 */
const getTblName = node => node.parentNameData[NODE_TYPES.TBL]

/**
 * @param {string} param.type - node group type
 * @param {string} param.schemaName - schema name
 * @param {string} [param.tblName] - table name
 * @param {boolean} [param.nodeAttrs.onlyIdentifier] - If it's true, it queries only the name of the node
 * @param {boolean} [param.nodeAttrs.onlyIdentifierWithParents] - query includes
 * the identifier name and its parent identifier names
 * @returns {string} SQL of the node group using for fetching its children nodes
 */
function genNodeGroupSQL({
    type,
    schemaName,
    tblName = '',
    nodeAttrs = { onlyIdentifier: false, onlyIdentifierWithParents: false },
}) {
    let colKey = NODE_NAME_KEYS[NODE_GROUP_CHILD_TYPES[type]],
        cols = '',
        from = '',
        cond = ''
    const { TBL_G, VIEW_G, SP_G, FN_G, TRIGGER_G, COL_G, IDX_G } = NODE_GROUP_TYPES
    switch (type) {
        case TBL_G:
        case VIEW_G:
            cols = `${colKey}, CREATE_TIME, TABLE_TYPE, TABLE_ROWS, ENGINE`
            from = 'FROM information_schema.TABLES'
            cond = `WHERE TABLE_SCHEMA = '${schemaName}' AND TABLE_TYPE ${
                type === TBL_G ? '=' : '!='
            } 'BASE TABLE'`
            break
        case FN_G:
            cols = `${colKey}, DTD_IDENTIFIER, IS_DETERMINISTIC, SQL_DATA_ACCESS, CREATED`
            from = 'FROM information_schema.ROUTINES'
            cond = `WHERE ROUTINE_TYPE = 'FUNCTION' AND ROUTINE_SCHEMA = '${schemaName}'`
            break
        case SP_G:
            cols = `${colKey}, IS_DETERMINISTIC, SQL_DATA_ACCESS, CREATED`
            from = 'FROM information_schema.ROUTINES'
            cond = `WHERE ROUTINE_TYPE = 'PROCEDURE' AND ROUTINE_SCHEMA = '${schemaName}'`
            break
        case TRIGGER_G:
            cols = `${colKey}, CREATED, EVENT_MANIPULATION, ACTION_STATEMENT, ACTION_TIMING`

            from = 'FROM information_schema.TRIGGERS'
            cond = `WHERE TRIGGER_SCHEMA = '${schemaName}'`
            if (tblName) cond += ` AND EVENT_OBJECT_TABLE = '${tblName}'`
            break
        case COL_G:
            cols = `${colKey}, COLUMN_TYPE, COLUMN_KEY, PRIVILEGES`
            from = 'FROM information_schema.COLUMNS'
            cond = `WHERE TABLE_SCHEMA = '${schemaName}'`
            if (tblName) cond += ` AND TABLE_NAME = '${tblName}'`
            break
        case IDX_G:
            // eslint-disable-next-line vue/max-len
            cols = `${colKey}, COLUMN_NAME, NON_UNIQUE, SEQ_IN_INDEX, CARDINALITY, NULLABLE, INDEX_TYPE`
            from = 'FROM information_schema.STATISTICS'
            cond = `WHERE TABLE_SCHEMA = '${schemaName}'`
            if (tblName) cond += ` AND TABLE_NAME = '${tblName}'`
            break
    }

    if (nodeAttrs.onlyIdentifierWithParents)
        switch (type) {
            case TBL_G:
            case VIEW_G:
                cols = `${colKey}, TABLE_SCHEMA`
                break
            case FN_G:
            case SP_G:
                cols = `${colKey}, ROUTINE_SCHEMA`
                break
            case COL_G:
            case IDX_G:
                cols = `${colKey}, TABLE_SCHEMA, TABLE_NAME`
                break
            case TRIGGER_G:
                cols = `${colKey}, TRIGGER_SCHEMA, EVENT_OBJECT_TABLE`
                break
        }
    if (nodeAttrs.onlyIdentifier) cols = colKey
    return `SELECT ${cols} ${from} ${cond} ORDER BY ${colKey};`
}

/**
 * @param {Object} param.parentNode - parent node of the node group being generated
 * @param {String} param.type - type in NODE_GROUP_TYPES
 * @returns
 */
function genNodeGroup({ parentNode, type }) {
    return {
        id: `${parentNode.id}.${type}`,
        parentNameData: { ...parentNode.parentNameData, [type]: type },
        key: genNodeKey(),
        type,
        name: type,
        draggable: false,
        level: parentNode.level + 1,
        children: [],
    }
}

/**
 * @param {Array} param.treeData - Array of tree nodes to be updated
 * @param {Object} param.node - node with new value
 * @returns {Array} new tree data
 */
function deepReplaceNode({ treeData, node }) {
    const nodeId = typy(node, 'id').safeString
    return lodash.cloneDeepWith(treeData, value => {
        if (value && value.id === nodeId) return node
    })
}

/**
 * This function returns nodes data for schema sidebar and its completion list for the editor
 * @param {Object} param.queryResult - query result data.
 * @param {Object} param.nodeGroup -  A node group. (NODE_GROUP_TYPES)
 * @param {Object} [param.nodeAttrs] - node attributes
 * @returns {array} - nodes
 */
function genNodes({ queryResult = {}, nodeGroup = null, nodeAttrs }) {
    const type = nodeGroup ? NODE_GROUP_CHILD_TYPES[nodeGroup.type] : NODE_TYPES.SCHEMA
    const { fields = [], data = [] } = queryResult
    // fields return could be in lowercase if connection is via ODBC.
    const standardizedFields = fields.map(f => f.toUpperCase())
    const rows = map2dArr({ fields: standardizedFields, arr: data })
    const nameKey = NODE_NAME_KEYS[type]
    return rows.reduce((acc, row) => {
        acc.push(
            genNode({
                nodeGroup,
                data: row,
                type,
                name: row[nameKey],
                nodeAttrs,
            })
        )
        return acc
    }, [])
}

/**
 * A node in db_tree_map has several attrs but some attrs are mainly of UX purpose.
 * This function returns a minimized version of node containing only necessary attrs
 * for identifying purpose and restoring expanded state of the schemas sidebar
 * @param {Object} node - a node in db_tree_map
 * @returns {Object} minimized node
 */
function minimizeNode({ id, parentNameData, qualified_name, name, type, level }) {
    return {
        id,
        qualified_name,
        parentNameData,
        name,
        type,
        level,
    }
}

/**
 * @param {object} node
 * @returns {object}
 */
function genCompletionItem(node) {
    const { type, name } = node
    const schemaName = getSchemaName(node)
    const tblName = getTblName(node)
    let documentation = ''
    if (schemaName) documentation += `SCHEMA: ${schemaName}\n`
    if (tblName) documentation += `TABLE: ${tblName}\n`
    return {
        label: name,
        detail: type.toUpperCase(),
        documentation,
        insertText: name,
        type: type,
    }
}
const nodeTypes = Object.values(NODE_TYPES)

/**
 *
 * @param {array} tree - schema tree
 * @returns {array} completion items
 */
function genNodeCompletionItems(tree) {
    return lodash.flatMap(tree, node => {
        if (nodeTypes.includes(node.type)) {
            if (typy(node, 'children').safeArray.length === 0) return [genCompletionItem(node)]
            return [genCompletionItem(node), ...genNodeCompletionItems(node.children)]
        }
        return genNodeCompletionItems(node.children)
    })
}

export default {
    getSchemaName,
    getTblName,
    genNodeGroupSQL,
    genNodeGroup,
    deepReplaceNode,
    genNodes,
    minimizeNode,
    genCompletionItem,
    genNodeCompletionItems,
}
