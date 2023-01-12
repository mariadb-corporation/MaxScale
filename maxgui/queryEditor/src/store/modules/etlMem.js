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
import { query } from '@queryEditorSrc/api/query'
import queryHelper from '@queryEditorSrc/store/queryHelper'

export default {
    namespaced: true,
    state: {
        src_schema_tree: [],
    },
    mutations: {
        SET_SRC_SCHEMA_TREE(state, payload) {
            state.src_schema_tree = payload
        },
    },
    actions: {
        async fetchSrcSchemas({ getters, commit }) {
            const { id: connId } = EtlTask.getters('getActiveSrcConn')
            if (connId) {
                const [e, res] = await this.vue.$helpers.to(
                    query({ id: connId, body: { sql: getters.getSchemaSql } })
                )
                if (!e) {
                    const { nodes } = queryHelper.genNodeData({
                        queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]')
                            .safeObject,
                        nodeAttrs: {
                            isEmptyChildren: true,
                        },
                    })
                    commit('SET_SRC_SCHEMA_TREE', nodes)
                }
            }
            /**
             * TODO: Show an error message in a snackbar to tell the users to reconnect
             * as the connection may have been expired
             */
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
    },
    getters: {
        getSchemaSql: (state, getters, rootState) => {
            const { NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const col = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            return `SELECT ${col} FROM information_schema.SCHEMATA ORDER BY ${col}`
        },
    },
}
