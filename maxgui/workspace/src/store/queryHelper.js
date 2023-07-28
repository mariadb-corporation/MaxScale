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
import { NODE_TYPES } from '@wsSrc/store/config'
import { to } from '@share/utils/helpers'
import { t as typy } from 'typy'
import { tableParser } from '@wsSrc/utils/helpers'
import queries from '@wsSrc/api/queries'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'

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

/**
 * @param {string} param.connId - id of connection
 * @param {string[]} param.tableNodes - tables to be queried and parsed
 * @param {object} param.config - axios config
 * @returns {Promise<array>} parsed tables
 */
async function queryAndParseDDL({ connId, tableNodes, config }) {
    const [e, res] = await to(
        queries.post({
            id: connId,
            body: {
                sql: tableNodes.map(node => `SHOW CREATE TABLE ${node.qualified_name};`).join('\n'),
                max_rows: 0,
            },
            config,
        })
    )
    return [
        e,
        typy(res, 'data.data.attributes.results').safeArray.map((item, i) =>
            tableParser.parse({
                ddl: typy(item, 'data[0][1]').safeString,
                schema: tableNodes[i].parentNameData[NODE_TYPES.SCHEMA],
                autoGenId: true,
            })
        ),
    ]
}

export default {
    getChildNodeData,
    getNewTreeData,
    queryAndParseDDL,
}
