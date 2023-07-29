/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { to } from '@share/utils/helpers'
import { t as typy } from 'typy'
import { tableParser, quotingIdentifier as quoting } from '@wsSrc/utils/helpers'
import queries from '@wsSrc/api/queries'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import erdHelper from '@wsSrc/utils/erdHelper'

/**
 * @public
 * @param {String} param.connId - SQL connection ID
 * @param {Object} param.nodeGroup - A node group. (NODE_GROUP_TYPES)
 * @param {Object} [param.nodeAttrs] - node attributes
 * @param {Object} param.config - axios config
 * @returns {Promise<Array>} { nodes: [], completionItems: [] }
 */
async function getChildNodeData({ connId, nodeGroup, nodeAttrs, config }) {
    const sql = schemaNodeHelper.getNodeGroupSQL({ nodeAttrs, nodeGroup })
    const [e, res] = await to(queries.post({ id: connId, body: { sql }, config }))
    if (e) return { nodes: [], completionItems: [] }
    else {
        return schemaNodeHelper.genNodeData({
            queryResult: typy(res, 'data.data.attributes.results[0]').safeObject,
            nodeGroup,
            nodeAttrs,
        })
    }
}

/**
 * @public
 * @param {String} payload.connId - SQL connection ID
 * @param {Object} payload.nodeGroup - A node group. (NODE_GROUP_TYPES)
 * @param {Array} payload.data - Array of tree node to be updated
 * @param {Array} [payload.completionItems] - Array of completion items for editor
 * @param {Object} param.config - axios config
 * @returns {Promise<Array>} { data: {}, completionItems: [] }
 */
async function getNewTreeData({ connId, nodeGroup, data, completionItems = [], config }) {
    const { nodes, completionItems: childCmplItems } = await getChildNodeData({
        connId,
        nodeGroup,
        config,
    })
    return {
        data: schemaNodeHelper.deepReplaceNode({
            treeData: data,
            node: { ...nodeGroup, children: nodes },
        }),
        completionItems: [...completionItems, ...childCmplItems],
    }
}

function parseTables({ res, targets }) {
    return typy(res, 'data.data.attributes.results').safeArray.reduce((result, item, i) => {
        if (typy(item, 'data').safeArray.length) {
            const ddl = typy(item, 'data[0][1]').safeString
            const schema = targets[i].schema
            result.push(tableParser.parse({ ddl, schema, autoGenId: true }))
        }
        return result
    }, [])
}
/**
 * @param {string} param.connId - id of connection
 * @param {object[]} param.targets - target tables to be queried and parsed. e.g. [ {schema: 'test', tbl: 't1'} ]
 * @param {object} param.config - axios config
 * @param {object} param.charsetCollationMap
 * @returns {Promise<array>} parsed data
 */
async function queryAndParseDDL({ connId, targets, config, charsetCollationMap }) {
    const sql = targets
        .map(t => `SHOW CREATE TABLE ${quoting(t.schema)}.${quoting(t.tbl)};`)
        .join('\n')

    const [e, res] = await to(queries.post({ id: connId, body: { sql, max_rows: 0 }, config }))
    const tables = parseTables({ res, targets })
    return [
        e,
        tables.map(parsedTable =>
            erdHelper.genDdlEditorData({
                parsedTable,
                lookupTables: tables,
                charsetCollationMap,
            })
        ),
    ]
}

export default {
    getChildNodeData,
    getNewTreeData,
    queryAndParseDDL,
}
