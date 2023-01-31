/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import Worksheet from '@wsModels/Worksheet'
import { query, getAsyncResult } from '@wsSrc/api/query'
import { prepare, start } from '@wsSrc/api/etl'
import queryHelper from '@wsSrc/store/queryHelper'
import { ETL_CREATE_MODES } from '@wsSrc/store/config'

export default {
    namespaced: true,
    state: {
        src_schema_tree: [],
        create_mode: ETL_CREATE_MODES.NORMAL,
        migration_objs: [], // store migration objects for /etl/prepare
        etl_prepare_res: {}, // store etl/prepare results
        etl_res: {}, // etl/start results
    },
    mutations: {
        SET_SRC_SCHEMA_TREE(state, payload) {
            state.src_schema_tree = payload
        },
        SET_CREATE_MODE(state, payload) {
            state.create_mode = payload
        },
        SET_MIGRATION_OBJS(state, payload) {
            state.migration_objs = payload
        },
        SET_ETL_PREPARE_RES(state, payload) {
            state.etl_prepare_res = payload
        },
        SET_ETL_RES(state, payload) {
            state.etl_res = payload
        },
    },
    actions: {
        async fetchSrcSchemas({ getters, commit }) {
            const { $mxs_t, $helpers, $typy } = this.vue
            const active_etl_task_id = EtlTask.getters('getActiveEtlTaskWithRelation').id
            EtlTask.dispatch('pushLog', {
                id: active_etl_task_id,
                log: {
                    timestamp: new Date().valueOf(),
                    name: $mxs_t('info.retrievingSchemaObj'),
                },
            })
            const [e, res] = await $helpers.to(
                query({
                    id: EtlTask.getters('getActiveSrcConn').id,
                    body: { sql: getters.getSchemaSql },
                })
            )
            let logName = ''
            if (e) logName = $mxs_t('errors.retrieveSchemaObj')
            else {
                const result = $typy(res, 'data.data.attributes.results[0]').safeObject
                if ($typy(result, 'errno').isDefined) {
                    logName = $mxs_t('errors.retrieveSchemaObj')
                    logName += `\n${$helpers.queryResErrToStr(result)}`
                } else {
                    const { nodes } = queryHelper.genNodeData({
                        queryResult: result,
                        nodeAttrs: {
                            isEmptyChildren: true,
                        },
                    })
                    commit('SET_SRC_SCHEMA_TREE', nodes)
                    logName = $mxs_t('success.retrieved')
                }
            }
            EtlTask.dispatch('pushLog', {
                id: active_etl_task_id,
                log: { timestamp: new Date().valueOf(), name: logName },
            })
        },
        /**
         * For now, only TBL nodes can be migrated, so the nodeGroup must be a TBL_G node
         * @param {Object} nodeGroup - TBL_G node
         */
        async loadChildNodes({ state, rootState, commit }, nodeGroup) {
            const { id: connId } = EtlTask.getters('getActiveSrcConn')
            const {
                NODE_GROUP_TYPES: { TBL_G },
            } = rootState.mxsWorkspace.config
            switch (nodeGroup.type) {
                case TBL_G: {
                    const { nodes } = await queryHelper.getChildNodeData({
                        connId,
                        nodeGroup,
                        nodeAttrs: {
                            onlyName: true,
                            isLeaf: true,
                            activatable: false,
                        },
                    })
                    const tree = queryHelper.deepReplaceNode({
                        treeData: state.src_schema_tree,
                        node: { ...nodeGroup, children: nodes },
                    })
                    commit('SET_SRC_SCHEMA_TREE', tree)
                    break
                }
            }
        },
        /**
         * @param {String} id - etl task id
         */
        async getEtlCallRes({ dispatch, commit, rootState }, id) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const task = EtlTask.find(id)
            const queryId = $typy(task, 'meta.async_query_id').safeString
            const srcConn = EtlTask.getters('getSrcConnByEtlTaskId')(id)
            let etlStatus
            const [e, res] = await $helpers.to(getAsyncResult({ id: srcConn.id, queryId }))
            if (!e) {
                const {
                    ETL_STAGE_INDEX: { MIGR_SCRIPT, DATA_MIGR },
                    ETL_STATUS: { INITIALIZING, COMPLETE, ERROR },
                } = rootState.mxsWorkspace.config
                const results = $typy(res, 'data.data.attributes.results').safeObject
                let logMsg, mutationName
                switch (task.active_stage_index) {
                    case MIGR_SCRIPT:
                        mutationName = 'SET_ETL_PREPARE_RES'
                        commit('SET_ETL_RES', {})
                        break
                    case DATA_MIGR:
                        mutationName = 'SET_ETL_RES'
                        break
                }

                if (res.status === 202) {
                    commit(mutationName, results)
                    await this.vue.$helpers
                        .delay(2000)
                        .then(async () => await dispatch('getEtlCallRes', id))
                } else if (res.status === 201) {
                    const timestamp = new Date().valueOf()
                    const ok = $typy(results, 'ok').safeBoolean

                    switch (task.active_stage_index) {
                        case MIGR_SCRIPT: {
                            logMsg = $mxs_t(
                                ok ? 'success.prepared' : 'errors.failedToPrepareMigrationScript'
                            )

                            etlStatus = ok ? INITIALIZING : ERROR
                            commit('SET_ETL_RES', {})
                            break
                        }
                        case DATA_MIGR: {
                            logMsg = $mxs_t(ok ? 'success.migration' : 'errors.migration')
                            etlStatus = ok ? COMPLETE : ERROR
                            break
                        }
                    }

                    const error = $typy(results, 'error').safeString
                    if (error) logMsg += ` \n${error}`
                    EtlTask.dispatch('pushLog', { id, log: { timestamp, name: logMsg } })

                    commit(mutationName, results)
                }
            }
            EtlTask.update({
                where: id,
                data(obj) {
                    if (etlStatus) obj.status = etlStatus
                },
            })
        },
        /**
         * @param {String} param.id - etl task id
         * @param {Array} param.tables - tables for preparing etl or start etl
         */
        async handleEtlCall({ state, rootState }, { id, tables }) {
            const { $helpers, $typy, $mxs_t } = this.vue

            const srcConn = EtlTask.getters('getSrcConnByEtlTaskId')(id)
            const destConn = EtlTask.getters('getDestConnByEtlTaskId')(id)

            const task = EtlTask.find(id)

            let logName,
                apiAction,
                status,
                timestamp = new Date().valueOf()

            let body = {
                target: destConn.id,
                type: srcConn.meta.src_type,
                tables,
            }

            const {
                ETL_STAGE_INDEX: { MIGR_SCRIPT, DATA_MIGR },
                ETL_STATUS: { RUNNING },
            } = rootState.mxsWorkspace.config

            switch (task.active_stage_index) {
                case MIGR_SCRIPT: {
                    logName = $mxs_t('info.preparingMigrationScript')
                    apiAction = prepare
                    status = RUNNING
                    body.create_mode = state.create_mode
                    break
                }
                case DATA_MIGR: {
                    logName = $mxs_t('info.startingMigration')
                    apiAction = start
                    status = RUNNING
                    break
                }
            }
            EtlTask.update({
                where: id,
                data(obj) {
                    obj.status = status
                    delete obj.meta.async_query_id
                },
            })

            const [e, res] = await $helpers.to(
                apiAction({
                    id: srcConn.id,
                    body,
                })
            )

            if (!e) {
                EtlTask.update({
                    where: id,
                    data(obj) {
                        // Persist query id
                        obj.meta.async_query_id = $typy(res, 'data.data.id').safeString
                    },
                })
                EtlTask.dispatch('pushLog', {
                    id: id,
                    log: {
                        timestamp,
                        name: logName,
                    },
                })
            }
        },
    },
    getters: {
        getSchemaSql: (state, getters, rootState) => {
            const { NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const col = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            return `SELECT ${col} FROM information_schema.SCHEMATA ORDER BY ${col}`
        },
        getMigrationPrepareScript: state => {
            const { tables = [] } = state.etl_prepare_res || {}
            return tables
        },
        getMigrationResTable: state => {
            const { tables = [] } = state.etl_res || {}
            return tables
        },
        getMigrationStage: state => {
            const { stage = ' []' } = state.etl_res || {}
            return stage
        },
        hasErrAtCreation: (state, getters, rootState) => {
            const { CREATE } = rootState.mxsWorkspace.config.ETL_API_STAGES
            const { stage = '' } = state.etl_res || {}
            return stage === CREATE
        },
        areConnsAlive: () => {
            const id = Worksheet.getters('getActiveEtlTaskId')
            const srcConn = EtlTask.getters('getSrcConnByEtlTaskId')(id)
            const destConn = EtlTask.getters('getDestConnByEtlTaskId')(id)
            return Boolean(srcConn.id && destConn.id)
        },
    },
}
