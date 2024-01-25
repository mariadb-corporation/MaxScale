/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { to } from '@share/utils/helpers'
import { t as typy } from 'typy'
import { tableParser, quotingIdentifier as quoting } from '@wsSrc/utils/helpers'
import { NODE_TYPES, NODE_GROUP_TYPES } from '@wsSrc/constants'
import queries from '@wsSrc/api/queries'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import erdHelper from '@wsSrc/utils/erdHelper'

/**
 * @public
 * @param {String} param.connId - SQL connection ID
 * @param {Object} param.nodeGroup - A node group. (NODE_GROUP_TYPES)
 * @param {Object} [param.nodeAttrs] - node attributes
 * @param {Object} param.config - axios config
 * @returns {Promise<Array>} nodes
 */
async function getChildNodes({ connId, nodeGroup, nodeAttrs, config }) {
    const sql = schemaNodeHelper.genNodeGroupSQL({
        type: nodeGroup.type,
        schemaName: schemaNodeHelper.getSchemaName(nodeGroup),
        tblName: schemaNodeHelper.getTblName(nodeGroup),
        nodeAttrs,
    })
    const [e, res] = await to(queries.post({ id: connId, body: { sql }, config }))
    if (e) return {}
    else {
        return schemaNodeHelper.genNodes({
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
 * @param {Object} param.config - axios config
 * @returns {Promise<Array>}
 */
async function getNewTreeData({ connId, nodeGroup, data, config }) {
    const nodes = await getChildNodes({
        connId,
        nodeGroup,
        config,
    })
    return schemaNodeHelper.deepReplaceNode({
        treeData: data,
        node: { ...nodeGroup, children: nodes },
    })
}

function stringifyQueryResErr(result) {
    return Object.keys(result)
        .map(key => `${key}: ${result[key]}`)
        .join('\n')
}

/**
 * @param {string} param.connId - id of connection
 * @param {string} param.type - NODE_TYPES
 * @param {object[]} param.qualifiedNames - e.g. ['`test`.`t1`']
 * @param {object} param.config - axios config
 * @returns {Promise<array>}
 */
async function queryDDL({ connId, type, qualifiedNames, config }) {
    const [e, res] = await to(
        queries.post({
            id: connId,
            body: {
                sql: qualifiedNames.map(item => `SHOW CREATE ${type} ${item};`).join('\n'),
            },
            config,
        })
    )
    const results = typy(res, 'data.data.attributes.results').safeArray
    const errors = results.reduce((errors, result) => {
        if (result.errno) errors.push({ detail: stringifyQueryResErr(result) })
        return errors
    }, [])
    if (errors.length) return [{ response: { data: { errors } } }, []]
    return [e, typy(res, 'data.data.attributes.results').safeArray]
}

function parseTables({ res, targets }) {
    return res.reduce((result, item, i) => {
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
async function queryAndParseTblDDL({ connId, targets, config, charsetCollationMap }) {
    const [e, res] = await queryDDL({
        connId,
        type: NODE_TYPES.TBL,
        qualifiedNames: targets.map(t => `${quoting(t.schema)}.${quoting(t.tbl)}`),
        config,
    })
    if (e) return [e, []]
    else {
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
}

async function fetchSchemaIdentifiers({ connId, config, schemaName }) {
    let results = []
    const nodeGroupTypes = Object.values(NODE_GROUP_TYPES)
    const sql = nodeGroupTypes
        .map(type =>
            schemaNodeHelper.genNodeGroupSQL({
                type,
                schemaName,
                tblName: '',
                nodeAttrs: { onlyIdentifierWithParents: true },
            })
        )
        .join('\n')
    const [e, res] = await to(queries.post({ id: connId, body: { sql }, config }))
    if (!e) results = typy(res, 'data.data.attributes.results').safeArray
    return results
}

export default {
    getChildNodes,
    getNewTreeData,
    queryAndParseTblDDL,
    fetchSchemaIdentifiers,
}
