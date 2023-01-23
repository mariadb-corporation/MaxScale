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

export default {
    namespaced: true,
    state: {
        src_schema_tree: [],
        are_conns_alive: false,
        migration_objs: [], // store migration objects for prepare, start
    },
    mutations: {
        SET_SRC_SCHEMA_TREE(state, payload) {
            state.src_schema_tree = payload
        },
        SET_ARE_CONNS_ALIVE(state, payload) {
            state.are_conns_alive = payload
        },
        SET_MIGRATION_OBJS(state, payload) {
            state.migration_objs = payload
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
                logName = $mxs_t('info.retrieveSchemaObj')
            }
            EtlTask.dispatch('pushLog', {
                id: EtlTask.getters('getActiveEtlTaskWithRelation').id,
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
            EtlTask.update({
                where: id,
                data(obj) {
                    obj.meta.is_loading = true
                },
            })
            if (srcConn.id) {
                const [e, res] = await $helpers.to(getAsyncResult({ id: srcConn.id, queryId }))
                if (!e) {
                    const results = $typy(res, 'data.data.attributes.results').safeObject
                    const timestamp = new Date().valueOf()
                    if (results) {
                        const ok = $typy(results, 'ok').safeBoolean
                        commit('SET_MIGRATION_OBJS', results.tables)

                        const {
                            ETL_STAGE_INDEX: { MIGR_SCRIPT, DATA_MIGR },
                            ETL_STATUS: { COMPLETE, ERROR },
                        } = rootState.mxsWorkspace.config

                        let status, logMsg
                        switch (task.active_stage_index) {
                            case MIGR_SCRIPT: {
                                logMsg = $mxs_t(
                                    ok
                                        ? 'info.prepareMigrationScriptSuccessfully'
                                        : 'errors.failedToPrepareMigrationScript'
                                )

                                break
                            }
                            case DATA_MIGR: {
                                logMsg = $mxs_t(
                                    ok ? 'info.migrateSuccessfully' : 'errors.migrateFailed'
                                )
                                status = ok ? COMPLETE : ERROR
                                break
                            }
                        }

                        const error = $typy(results, 'error').safeString
                        if (error) logMsg += ` \n${error}`
                        EtlTask.dispatch('pushLog', { id, log: { timestamp, name: `${logMsg}` } })

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
                    } else
                        await this.vue.$helpers
                            .delay(2000)
                            .then(async () => await dispatch('getEtlCallRes', id))
                }
            }
        },
        /**
         * @param {String} param.id - etl task id
         * @param {Number} param.stageIdx - Index of ETL stage. Either MIGR_SCRIPT or DATA_MIGR stage index
         */
        async handleEtlCall({ state, rootState }, { id, stageIdx }) {
            const { $helpers, $typy, $mxs_t } = this.vue

            const srcConn = EtlTask.getters('getSrcConnByEtlTaskId')(id)
            const destConn = EtlTask.getters('getDestConnByEtlTaskId')(id)

            let logName, apiAction, status

            const {
                ETL_STAGE_INDEX: { MIGR_SCRIPT, DATA_MIGR },
                ETL_STATUS: { RUNNING },
            } = rootState.mxsWorkspace.config

            switch (stageIdx) {
                case MIGR_SCRIPT: {
                    logName = $mxs_t('info.preparingMigrationScript')
                    apiAction = prepare
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
                    if (status) obj.status = status
                },
            })
            const [e, res] = await $helpers.to(
                apiAction({
                    id: srcConn.id,
                    body: {
                        target: destConn.id,
                        type: srcConn.meta.src_type,
                        tables: state.migration_objs,
                    },
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
                        timestamp: new Date().valueOf(),
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
    },
}
