/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { immutableUpdate } from 'utils/helpers'
import { APP_CONFIG } from 'utils/constants'

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

/**
 * @public
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
 * @public
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
 * @public
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
 * @public
 * @param {Object} active_sql_conn - current connecting resource
 * @param {String} nodeId - node id .i.e schema_name.tbl_name
 * @param {Object} vue - vue instance
 * @param {Object} $queryHttp - $queryHttp axios instance
 * @returns {Object} - returns object row data
 */
async function queryTblOptsData({ active_sql_conn, nodeId, vue, $queryHttp }) {
    const schemas = nodeId.split('.')
    const db = schemas[0]
    const tblName = schemas[1]
    const cols =
        // eslint-disable-next-line vue/max-len
        'table_name, ENGINE as table_engine, character_set_name as table_charset, table_collation, table_comment'
    const sql = `SELECT ${cols} FROM information_schema.tables t
JOIN information_schema.collations c ON t.table_collation = c.collation_name
WHERE table_schema = "${db}" AND table_name = "${tblName}";`
    let tblOptsRes = await $queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
        sql,
    })
    const tblOptsRows = vue.$help.getObjectRows({
        columns: tblOptsRes.data.data.attributes.results[0].fields,
        rows: tblOptsRes.data.data.attributes.results[0].data,
    })
    return tblOptsRows[0]
}
/**
 * @public
 * @param {Object} active_sql_conn - current connecting resource
 * @param {String} nodeId - node id .i.e schema_name.tbl_name
 * @param {Object} $queryHttp - $queryHttp axios instance
 * @returns {Object} - returns object data contains `data` and `fields`
 */
async function queryColsOptsData({ active_sql_conn, nodeId, $queryHttp }) {
    const schemas = nodeId.split('.')
    const db = schemas[0]
    const tblName = schemas[1]
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
    const colsOptsRes = await $queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
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

/**
 * @private
 * This helps to mutate flat state
 * @param {Object} moduleState  module state to be mutated
 * @param {Object} data  key/value state, can be more than one key
 */
function mutate_flat_states({ moduleState, data }) {
    Object.keys(data).forEach(key => (moduleState[key] = data[key]))
}

/**
 * @private
 * This function helps to synchronize the active wke in worksheets_arr with provided data
 * @param {Object} payload.scope - scope aka (this)
 * @param {Object} payload.data - partial modification of a wke in worksheets_arr
 * @param {Object} payload.active_wke_id - active_wke_id
 */
function sync_to_worksheets_arr({ scope, data, active_wke_id }) {
    const worksheets_arr = scope.state.wke.worksheets_arr
    const idx = worksheets_arr.findIndex(wke => wke.id === active_wke_id)
    scope.state.wke.worksheets_arr = scope.vue.$help.immutableUpdate(worksheets_arr, {
        [idx]: { $set: { ...worksheets_arr[idx], ...data } },
    })
}
/**
 * @private
 * This function mutates (payload.data) to the provided mutateStateModule
 * then synchronizes it to the active wke in worksheets_arr with
 * @param {Object} payload.scope - scope aka (this)
 * @param {Object} payload.mutateStateModule - state module to be mutated
 * @param {Object} payload.data - partial modification of a wke object
 * @param {Object} payload.active_wke_id - active_wke_id
 */
function mutate_sync_wke({ scope, mutateStateModule, data, active_wke_id }) {
    mutate_flat_states({ moduleState: mutateStateModule, data })
    sync_to_worksheets_arr({ scope, data, active_wke_id })
}

/**
 * @public
 * This function helps to generate vuex mutations for states are
 * stored in memory, i.e. states return from memStates().
 * The name of mutation follows this pattern SET_STATE_NAME.
 * e.g. Mutation for active_sql_conn state is SET_ACTIVE_SQL_CONN
 * @param {Object} statesToBeSynced
 * @returns {Object} - returns vuex mutations
 */
function syncedStateMutationsCreator(statesToBeSynced) {
    return Object.keys(statesToBeSynced).reduce(
        (mutations, key) => ({
            ...mutations,
            [`SET_${key.toUpperCase()}`]: function(state, { payload, active_wke_id }) {
                mutate_sync_wke({
                    scope: this,
                    mutateStateModule: state,
                    data: { [key]: payload },
                    active_wke_id,
                })
            },
        }),
        {}
    )
}
/**
 * @public
 * This function helps to generate a mutation to sync wke properties to flat states
 * @param {Object} statesToBeSynced
 * @returns {Object} - returns vuex mutation
 */
function syncWkeToFlatStateMutationCreator(statesToBeSynced) {
    return {
        /**
         * Sync wke properties to flat states
         * @param {Object} state - vuex state
         * @param {Object} wke - wke object
         */
        SYNC_WITH_WKE: function(state, wke) {
            mutate_flat_states({
                moduleState: state,
                data: this.vue.$help.lodash.pickBy(wke, (v, key) =>
                    Object.keys(statesToBeSynced).includes(key)
                ),
            })
        },
    }
}

/**
 * @public
 * Mutations creator for states storing in hash map structure (storing in memory).
 * The state uses worksheet id as key or session id. This helps to preserve multiple worksheet's
 * data or session's data in memory.
 * The name of mutation follows this pattern SET_STATE_NAME or PATCH_STATE_NAME.
 * e.g. Mutation for is_conn_busy_map state is SET_IS_CONN_BUSY_MAP
 * @param {Object} param.mutationTypesMap - mutation type keys map for states storing in memory. Either SET or PATCH
 * @returns {Object} - returns mutations for provided keys from mutationTypesMap
 */
function memStatesMutationCreator(mutationTypesMap) {
    return Object.keys(mutationTypesMap).reduce((mutations, stateName) => {
        const mutationType = mutationTypesMap[stateName]
        return {
            ...mutations,
            /**
             * if payload is not provided, the id (wke_id or session_id) key will be removed from the map
             * @param {String} param.id - wke_id or session_id
             */
            [`${mutationType}_${stateName.toUpperCase()}`]: function(state, { id, payload }) {
                if (!payload) this.vue.$delete(state[stateName], id)
                else {
                    switch (mutationType) {
                        case 'SET':
                            state[stateName] = { ...state[stateName], [id]: payload }
                            break
                        case 'PATCH':
                            state[stateName] = {
                                ...state[stateName],
                                ...{ [id]: { ...state[stateName][id], ...payload } },
                            }
                            break
                    }
                }
            },
        }
    }, {})
}
/**
 * @public
 * This helps to commit mutations provided by mutationTypesMap to delete the states storing in memory for a worksheet
 * or session.
 * @param {Object} param.namespace - module namespace. i.e. editor, queryConn, queryResult, schemaSidebar
 * @param {Function} param.commit - vuex commit function
 * @param {String} param.id - wke_id or session_id
 * @param {Object} param.mutationTypesMap - mutation type keys map for states storing in memory. Either SET or PATCH
 */
function releaseMemory({ namespace, commit, id, mutationTypesMap }) {
    Object.keys(mutationTypesMap).forEach(key => {
        commit(`${namespace}/${mutationTypesMap[key]}_${key.toUpperCase()}`, { id }, { root: true })
    })
}
export default {
    getClientConnIds,
    updateDbChild,
    updateTblChild,
    queryTblOptsData,
    queryColsOptsData,
    syncedStateMutationsCreator,
    syncWkeToFlatStateMutationCreator,
    memStatesMutationCreator,
    releaseMemory,
}
