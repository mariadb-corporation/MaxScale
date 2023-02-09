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
import QueryConn from '@wsModels/QueryConn'
import { query } from '@wsSrc/api/query'
import queryHelper from '@wsSrc/store/queryHelper'
import { ETL_CREATE_MODES, ETL_DEF_POLLING_INTERVAL } from '@wsSrc/store/config'

export default {
    namespaced: true,
    state: {
        src_schema_tree: [],
        create_mode: ETL_CREATE_MODES.NORMAL,
        migration_objs: [], // store migration objects for /etl/prepare
        polling_interval: ETL_DEF_POLLING_INTERVAL,
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
        SET_POLLING_INTERVAL(state, payload) {
            state.polling_interval = payload
        },
    },
    actions: {
        async fetchSrcSchemas({ getters, commit }) {
            const { $mxs_t, $helpers, $typy } = this.vue
            const taskId = EtlTask.getters('getActiveEtlTask').id
            EtlTask.dispatch('pushLog', {
                id: taskId,
                log: {
                    timestamp: new Date().valueOf(),
                    name: $mxs_t('info.retrievingSchemaObj'),
                },
            })
            const [e, res] = await $helpers.to(
                query({
                    id: QueryConn.getters('getActiveSrcConn').id,
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
                id: taskId,
                log: { timestamp: new Date().valueOf(), name: logName },
            })
        },
        /**
         * For now, only TBL nodes can be migrated, so the nodeGroup must be a TBL_G node
         * @param {Object} nodeGroup - TBL_G node
         */
        async loadChildNodes({ state, rootState, commit }, nodeGroup) {
            const { id: connId } = QueryConn.getters('getActiveSrcConn')
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
    },
    getters: {
        getSchemaSql: (state, getters, rootState) => {
            const { NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const col = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            return `SELECT ${col} FROM information_schema.SCHEMATA ORDER BY ${col}`
        },
        isSrcAlive: () => Boolean(QueryConn.getters('getActiveSrcConn').id),
        isDestAlive: () => Boolean(QueryConn.getters('getActiveDestConn').id),
        areConnsAlive: (state, getters) => getters.isSrcAlive && getters.isDestAlive,
    },
}
