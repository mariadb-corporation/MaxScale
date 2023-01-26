/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import { query, getAsyncResult } from '@queryEditorSrc/api/query'
import { prepare, start } from '@queryEditorSrc/api/etl'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import { ETL_CREATE_MODES } from '@queryEditorSrc/store/config'

export default {
    namespaced: true,
    state: {
        src_schema_tree: [],
        are_conns_alive: false,
        create_mode: ETL_CREATE_MODES.NORMAL,
        migration_objs: [], // store migration objects for /etl/prepare
        etl_prepare_res: {}, // store etl/prepare results
        etl_res: {}, // etl/start results
    },
    mutations: {
        SET_SRC_SCHEMA_TREE(state, payload) {
            state.src_schema_tree = payload
        },
        SET_ARE_CONNS_ALIVE(state, payload) {
            state.are_conns_alive = payload
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
        /**
         * Validate active ETL task connections
         * @param {Boolean} silentValidation - silent validation (without showing snackbar message)
         */
        async validateActiveEtlTaskConns({ commit }, { silentValidation = false } = {}) {
            await QueryConn.dispatch('validateConns', {
                persistentConns: EtlTask.getters('getActiveEtlConns'),
            })
            const { id } = EtlTask.getters('getActiveEtlTaskWithRelation')
            const srcConn = EtlTask.getters('getSrcConnByEtlTaskId')(id)
            const destConn = EtlTask.getters('getDestConnByEtlTaskId')(id)
            const areConnsAlive = Boolean(srcConn.id && destConn.id)
            commit('SET_ARE_CONNS_ALIVE', areConnsAlive)
            if (!areConnsAlive && !silentValidation)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('errors.connsExpired')],
                        type: 'error',
                    },
                    { root: true }
                )
        },
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
                const { nodes } = queryHelper.genNodeData({
                    queryResult: $typy(res, 'data.data.attributes.results[0]').safeObject,
                    nodeAttrs: {
                        isEmptyChildren: true,
                    },
                })
                commit('SET_SRC_SCHEMA_TREE', nodes)
                logName = $mxs_t('success.retrieved')
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
            if (srcConn.id) {
                EtlTask.update({
                    where: id,
                    data(obj) {
                        obj.meta.is_loading = true
                    },
                })
                const [e, res] = await $helpers.to(getAsyncResult({ id: srcConn.id, queryId }))
                if (!e) {
                    if (res.status === 202)
                        await this.vue.$helpers
                            .delay(2000)
                            .then(async () => await dispatch('getEtlCallRes', id))
                    else if (res.status === 201) {
                        const results = $typy(res, 'data.data.attributes.results').safeObject
                        const timestamp = new Date().valueOf()
                        const ok = $typy(results, 'ok').safeBoolean

                        const {
                            ETL_STAGE_INDEX: { MIGR_SCRIPT, DATA_MIGR },
                            ETL_STATUS: { COMPLETE, ERROR },
                        } = rootState.mxsWorkspace.config

                        let status, logMsg, mutationName
                        switch (task.active_stage_index) {
                            case MIGR_SCRIPT: {
                                logMsg = $mxs_t(
                                    ok
                                        ? 'success.prepared'
                                        : 'errors.failedToPrepareMigrationScript'
                                )
                                mutationName = 'SET_ETL_PREPARE_RES'
                                commit('SET_ETL_RES', {})
                                break
                            }
                            case DATA_MIGR: {
                                logMsg = $mxs_t(ok ? 'success.migration' : 'errors.migration')
                                status = ok ? COMPLETE : ERROR
                                mutationName = 'SET_ETL_RES'
                                break
                            }
                        }

                        const error = $typy(results, 'error').safeString
                        if (error) logMsg += ` \n${error}`
                        EtlTask.dispatch('pushLog', { id, log: { timestamp, name: logMsg } })

                        commit(mutationName, results)
                        commit(
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            { text: [logMsg], type: ok ? 'success' : 'error' },
                            { root: true }
                        )

                        EtlTask.update({
                            where: id,
                            data(obj) {
                                obj.meta.is_loading = false
                                if (status) obj.status = status
                            },
                        })
                    }
                }
            }
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
                ETL_STATUS: { RUNNING, INITIALIZING },
            } = rootState.mxsWorkspace.config

            switch (task.active_stage_index) {
                case MIGR_SCRIPT: {
                    logName = $mxs_t('info.preparingMigrationScript')
                    apiAction = prepare
                    status = INITIALIZING
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
                    obj.meta.is_loading = true
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
    },
}
