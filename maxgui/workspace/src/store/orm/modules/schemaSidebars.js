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
import Worksheet from '@workspaceSrc/store/orm/models/Worksheet'
import WorksheetTmp from '@workspaceSrc/store/orm/models/WorksheetTmp'
import SchemaSidebar from '@workspaceSrc/store/orm/models/SchemaSidebar'
import QueryConn from '@workspaceSrc/store/orm/models/QueryConn'
import QueryTab from '@workspaceSrc/store/orm/models/QueryTab'
import { lodash } from '@share/utils/helpers'
import queryHelper from '@workspaceSrc/store/queryHelper'
import { query } from '@workspaceSrc/api/query'

export default {
    namespaced: true,
    actions: {
        async initialFetch({ dispatch }) {
            await dispatch('fetchSchemas')
            await QueryConn.dispatch('updateActiveDb')
        },
        /**
         * @param {Object} nodeGroup - A node group. (NODE_GROUP_TYPES)
         */
        async loadChildNodes({ getters }, nodeGroup) {
            const activeWkeId = Worksheet.getters('getActiveWkeId')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
            const { data, completionItems } = await queryHelper.getNewTreeData({
                connId,
                nodeGroup,
                data: getters.getDbTreeData,
                completionItems: getters.getSchemaCompletionItems,
            })
            WorksheetTmp.update({
                where: activeWkeId,
                data(obj) {
                    obj.db_tree = data
                    obj.completion_items = completionItems
                },
            })
        },
        async fetchSchemas({ getters, rootState }) {
            const activeWkeId = Worksheet.getters('getActiveWkeId')
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')
            const connId = activeQueryTabConn.id
            WorksheetTmp.update({
                where: activeWkeId,
                data: { loading_db_tree: true },
            })

            const [e, res] = await this.vue.$helpers.to(
                query({ id: connId, body: { sql: getters.getDbSql } })
            )
            if (e)
                WorksheetTmp.update({
                    where: activeWkeId,
                    data: { loading_db_tree: false },
                })
            else {
                const { nodes, completionItems } = queryHelper.genNodeData({
                    queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]').safeObject,
                })
                if (nodes.length) {
                    let data = nodes
                    let completion_items = completionItems

                    const groupNodes = Object.values(rootState.mxsWorkspace.config.NODE_GROUP_TYPES)
                    // fetch expanded_nodes
                    for (const nodeGroup of getters.getExpandedNodes) {
                        if (groupNodes.includes(nodeGroup.type)) {
                            const {
                                data: newData,
                                completionItems: newCompletionItems,
                            } = await queryHelper.getNewTreeData({
                                connId,
                                nodeGroup,
                                data,
                                completionItems: completion_items,
                            })
                            data = newData
                            completion_items = newCompletionItems
                        }
                    }
                    WorksheetTmp.update({
                        where: activeWkeId,
                        data(obj) {
                            obj.loading_db_tree = false
                            obj.completion_items = completion_items
                            obj.db_tree_of_conn = activeQueryTabConn.name
                            obj.db_tree = data
                        },
                    })
                }
            }
        },
    },
    getters: {
        // sidebar getters
        getDbSql: (state, getters, rootState) => {
            const { SYS_SCHEMAS, NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const schema = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            let sql = 'SELECT * FROM information_schema.SCHEMATA'
            if (!rootState.prefAndStorage.query_show_sys_schemas_flag)
                sql += ` WHERE ${schema} NOT IN(${SYS_SCHEMAS.map(db => `'${db}'`).join(',')})`
            sql += ` ORDER BY ${schema};`
            return sql
        },
        getSchemaSidebar: () => SchemaSidebar.find(Worksheet.getters('getActiveWkeId')) || {},
        getExpandedNodes: (state, getters) => getters.getSchemaSidebar.expanded_nodes || [],
        getFilterTxt: (state, getters) => getters.getSchemaSidebar.filter_txt || '',
        // Getters for mem states
        getLoadingDbTree: () => Worksheet.getters('getWorksheetMem').loading_db_tree || false,
        getSchemaCompletionItems: () =>
            lodash.uniqBy(Worksheet.getters('getWorksheetMem').completion_items || [], 'label'),
        getDbTreeOfConn: () => Worksheet.getters('getWorksheetMem').db_tree_of_conn || '',
        getDbTreeData: () => Worksheet.getters('getWorksheetMem').db_tree || {},
        getActivePrvwNode: () => QueryTab.getters('getActiveQueryTabMem').active_prvw_node || {},
        getActivePrvwNodeFQN: (state, getters) => getters.getActivePrvwNode.qualified_name || '',
    },
}
