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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import WorksheetTmp from '@queryEditorSrc/store/orm/models/WorksheetTmp'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import { lodash } from '@share/utils/helpers'
import queryHelper from '@queryEditorSrc/store/queryHelper'

export default {
    namespaced: true,
    actions: {
        async initialFetch({ dispatch }) {
            await dispatch('fetchSchemas')
            await QueryConn.dispatch('updateActiveDb')
        },
        /**
         * @param {Object} payload.nodeGroup - A node group. (NODE_GROUP_TYPES)
         * @param {Array} payload.data - Array of tree node to be updated
         * @param {Array} payload.completionList - Array of completion list for editor
         * @returns {Array} { data: {}, completionList: [] }
         */
        async getNewTreeData(_, { nodeGroup, data, completionList }) {
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
            const sql = queryHelper.getNodeGroupSQL(nodeGroup)
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${connId}/queries`, {
                    sql,
                })
            )
            if (e) return { data: {}, completionList: [] }
            else {
                const { nodes: children, cmpList: partCmpList } = queryHelper.genNodeData({
                    queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]').safeObject,
                    nodeGroup,
                })
                return {
                    data: queryHelper.deepReplaceNode({
                        treeData: data,
                        nodeId: nodeGroup.id,
                        children,
                    }),
                    completionList: [...completionList, ...partCmpList],
                }
            }
        },
        /**
         * @param {Object} nodeGroup - A node group. (NODE_GROUP_TYPES)
         */
        async loadChildNodes({ dispatch, getters }, nodeGroup) {
            const activeWkeId = Worksheet.getters('getActiveWkeId')
            const { data, completionList } = await dispatch('getNewTreeData', {
                nodeGroup,
                data: getters.getDbTreeData,
                completionList: getters.getDbCmplList,
            })
            WorksheetTmp.update({
                where: activeWkeId,
                data(obj) {
                    obj.db_tree = data
                    obj.completion_list = completionList
                },
            })
        },
        async fetchSchemas({ dispatch, getters, rootState }) {
            const activeWkeId = Worksheet.getters('getActiveWkeId')
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')

            WorksheetTmp.update({
                where: activeWkeId,
                data: { loading_db_tree: true },
            })

            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${activeQueryTabConn.id}/queries`, {
                    sql: getters.getDbSql,
                })
            )
            if (e)
                WorksheetTmp.update({
                    where: activeWkeId,
                    data: { loading_db_tree: false },
                })
            else {
                const { nodes, cmpList } = queryHelper.genNodeData({
                    queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]').safeObject,
                })
                if (nodes.length) {
                    let data = nodes
                    let completion_list = cmpList

                    const groupNodes = Object.values(
                        rootState.queryEditorConfig.config.NODE_GROUP_TYPES
                    )
                    // fetch expanded_nodes
                    for (const nodeGroup of getters.getExpandedNodes) {
                        if (groupNodes.includes(nodeGroup.type)) {
                            const { data: newData, completionList } = await dispatch(
                                'getNewTreeData',
                                {
                                    nodeGroup,
                                    data,
                                    completionList: completion_list,
                                }
                            )
                            data = newData
                            completion_list = completionList
                        }
                    }
                    WorksheetTmp.update({
                        where: activeWkeId,
                        data(obj) {
                            obj.loading_db_tree = false
                            obj.completion_list = completion_list
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
            const { SYS_SCHEMAS: SYS_S } = rootState.queryEditorConfig.config
            let sql = 'SELECT * FROM information_schema.SCHEMATA'
            if (!rootState.queryPersisted.query_show_sys_schemas_flag)
                sql += ` WHERE SCHEMA_NAME NOT IN(${SYS_S.map(db => `'${db}'`).join(',')})`
            sql += ' ORDER BY SCHEMA_NAME;'
            return sql
        },
        getSchemaSidebar: () => SchemaSidebar.find(Worksheet.getters('getActiveWkeId')) || {},
        getExpandedNodes: (state, getters) => getters.getSchemaSidebar.expanded_nodes || [],
        getFilterTxt: (state, getters) => getters.getSchemaSidebar.filter_txt || '',
        // Getters for mem states
        getLoadingDbTree: () => Worksheet.getters('getWorksheetMem').loading_db_tree || false,
        getDbCmplList: () =>
            lodash.uniqBy(Worksheet.getters('getWorksheetMem').completion_list || [], 'label'),
        getDbTreeOfConn: () => Worksheet.getters('getWorksheetMem').db_tree_of_conn || '',
        getDbTreeData: () => Worksheet.getters('getWorksheetMem').db_tree || {},
        getActivePrvwNode: () => QueryTab.getters('getActiveQueryTabMem').active_prvw_node || {},
        getActivePrvwNodeFQN: (state, getters) => getters.getActivePrvwNode.qualified_name || '',
    },
}
