/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { immutableUpdate } from 'utils/helpers'
import { APP_CONFIG } from 'utils/constants'
// private functions
/**
 * @private
 * @param {String} payload.dbName - Database name to be found
 * @param {Array} payload.db_tree - Array of tree node
 * @returns {Number} index of target db
 */
const getDbIdx = ({ dbName, db_tree }) => db_tree.findIndex(db => db.name === dbName)

/**
 * @private
 * @param {String} payload.dbIdx - database index having the child
 * @param {Array} payload.db_tree - Array of tree node
 *  @param {Array} payload.childType - Children type of the database node. i.e. Tables||Stored Procedures
 * @returns {Number} index of Tables or Stored Procedures
 */
const getIdxOfDbChildNode = ({ dbIdx, db_tree, childType }) =>
    db_tree[dbIdx].children.findIndex(dbChildrenNode => dbChildrenNode.type === childType)

// Public functions
/**
 * @param {String} prefixName - prefix name of the connection cookie. i.e. conn_id_body_
 * @returns {Array} an array of connection ids found from cookies
 */
function getClientConnIds(prefixName = 'conn_id_body_') {
    let value = '; ' + document.cookie
    let cookiesStr = value.split('; ')
    const connCookies = cookiesStr.filter(p => p.includes(prefixName))
    const connIds = []
    connCookies.forEach(str => {
        const parts = str.split('=')
        if (parts.length === 2) connIds.push(parts[0].replace(prefixName, ''))
    })
    return connIds
}

/**
 * Use this function to update database node children. i.e. Populating children for
 * Tables||Stored Procedures node
 * @param {Array} payload.db_tree - Array of tree node to be updated
 * @param {String} payload.dbName - Database name
 * @param {String} payload.childType - Child type of the node to be updated. i.e. Tables||Stored Procedures
 * @param {Array} payload.gch - Array of grand child nodes (Table|Store Procedure) to be added
 * @returns {Array} new array of db_tree
 */
function updateDbChild({ db_tree, dbName, childType, gch }) {
    try {
        const dbIdx = getDbIdx({ dbName, db_tree })
        // Tables or Stored Procedures node
        const childIndex = getIdxOfDbChildNode({ dbIdx, db_tree, childType })
        const new_db_tree = immutableUpdate(db_tree, {
            [dbIdx]: { children: { [childIndex]: { children: { $set: gch } } } },
        })
        return new_db_tree
    } catch (e) {
        return {}
    }
}

/**
 * Use this function to update table node children. i.e. Populating children for
 * `Columns` node or `Triggers` node
 * @param {Array} payload.db_tree - Array of tree node to be updated
 * @param {String} payload.dbName - Database name
 * @param {String} payload.tblName - Table name
 * @param {String} payload.childType Child type of the node to be updated. i.e. Columns or Triggers node
 * @param {Array} payload.gch -  Array of grand child nodes (column or trigger)
 * @returns {Array} new array of db_tree
 */
function updateTblChild({ db_tree, dbName, tblName, gch, childType }) {
    try {
        const dbIdx = getDbIdx({ dbName, db_tree })
        // idx of Tables node
        const idxOfTablesNode = getIdxOfDbChildNode({
            dbIdx,
            db_tree,
            childType: APP_CONFIG.SQL_NODE_TYPES.TABLES,
        })
        const tblIdx = db_tree[dbIdx].children[idxOfTablesNode].children.findIndex(
            tbl => tbl.name === tblName
        )

        const dbChildNodes = db_tree[dbIdx].children // Tables and Stored Procedures nodes
        const tblNode = dbChildNodes[idxOfTablesNode].children[tblIdx] // a table node
        // Columns and Triggers node
        const idxOfChild = tblNode.children.findIndex(node => node.type === childType)

        const new_db_tree = immutableUpdate(db_tree, {
            [dbIdx]: {
                children: {
                    [idxOfTablesNode]: {
                        children: {
                            [tblIdx]: {
                                children: {
                                    [idxOfChild]: { children: { $set: gch } },
                                },
                            },
                        },
                    },
                },
            },
        })
        return new_db_tree
    } catch (e) {
        return {}
    }
}

/**
 * @param {Object} curr_cnct_resource - current connecting resource
 * @param {String} nodeId - node id .i.e schema_name.tbl_name
 * @param {Object} vue - vue instance
 * @param {Object} $http - $http axios instance
 * @returns {Object} - returns object row data
 */
async function queryTblOptsData({ curr_cnct_resource, nodeId, vue, $http }) {
    const schemas = nodeId.split('.')
    const db = schemas[0]
    const tblName = schemas[1]
    const cols =
        // eslint-disable-next-line vue/max-len
        'table_name, ENGINE as table_engine, character_set_name as table_charset, table_collation, table_comment'
    const sql = `SELECT ${cols} FROM information_schema.tables t
JOIN information_schema.collations c ON t.table_collation = c.collation_name
WHERE table_schema = "${db}" AND table_name = "${tblName}";`
    let tblOptsRes = await $http.post(`/sql/${curr_cnct_resource.id}/queries`, {
        sql,
    })
    const tblOptsRows = vue.$help.getObjectRows({
        columns: tblOptsRes.data.data.attributes.results[0].fields,
        rows: tblOptsRes.data.data.attributes.results[0].data,
    })
    return tblOptsRows[0]
}
/**
 * @param {Object} curr_cnct_resource - current connecting resource
 * @param {String} nodeId - node id .i.e schema_name.tbl_name
 * @param {Object} $http - $http axios instance
 * @returns {Object} - returns object data contains `data` and `fields`
 */
async function queryColsOptsData({ curr_cnct_resource, nodeId, $http }) {
    const schemas = nodeId.split('.')
    const db = schemas[0]
    const tblName = schemas[1]
    //TODO: Add G column
    /**
     * Exception for UQ column
     * It needs to LEFT JOIN statistics and table_constraints tables to get accurate UNIQUE INDEX from constraint_name.
     * LEFT JOIN statistics as it has column_name, index_name
     * LEFT JOIN table_constraints as it has constraint_name. There is a sub-query in table_constraints to get
     * get only rows having constraint_type = 'UNIQUE'.
     * Notice: UQ column returns UNIQUE INDEX name.
     *
     */
    const cols = `
    UUID() AS id,
    a.column_name,
    REGEXP_SUBSTR(UPPER(column_type), '[^)]*[)]?') AS column_type,
    IF(column_key LIKE '%PRI%', 'YES', 'NO') as PK,
    IF(is_nullable LIKE 'YES', 'NULL', 'NOT NULL') as NN,
    IF(column_type LIKE '%UNSIGNED%', 'UNSIGNED', '') as UN,
    IF(c.constraint_name IS NULL, '', c.constraint_name) as UQ,
    IF(column_type LIKE '%ZEROFILL%', 'ZEROFILL', '') as ZF,
    IF(extra LIKE '%AUTO_INCREMENT%', 'AUTO_INCREMENT', '') as AI,
    IF(
        UPPER(extra) REGEXP 'VIRTUAL|STORED',
        REGEXP_SUBSTR(UPPER(extra), 'VIRTUAL|STORED'),
        '(none)'
     ) AS generated,
    COALESCE(generation_expression, column_default, '') as 'default/expression',
    IF(character_set_name IS NULL, '', character_set_name) as charset,
    IF(collation_name IS NULL, '', collation_name) as collation,
    column_comment as comment
    `
    const colsOptsRes = await $http.post(`/sql/${curr_cnct_resource.id}/queries`, {
        sql: `
        SELECT ${cols} FROM information_schema.columns a
            LEFT JOIN information_schema.statistics b ON (
                a.table_schema = b.table_schema
                AND a.table_name = b.table_name
                AND a.column_name = b.column_name
            )
            LEFT JOIN (
                SELECT
                    table_name, table_schema, constraint_name
                FROM
                    information_schema.table_constraints
                WHERE
                    constraint_type = 'UNIQUE'
            ) c ON (
                a.table_name = c.table_name
                AND a.table_schema = c.table_schema
                AND b.index_name = c.constraint_name
            )
        WHERE
            a.table_schema='${db}'
            AND a.table_name='${tblName}'
        GROUP BY a.column_name
        ORDER BY a.ordinal_position;`,
    })
    return colsOptsRes.data.data.attributes.results[0]
}

export default {
    getClientConnIds,
    updateDbChild,
    updateTblChild,
    queryTblOptsData,
    queryColsOptsData,
}
