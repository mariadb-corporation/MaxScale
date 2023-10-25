/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import { lodash } from '@share/utils/helpers'
import queryHelper from '@wsSrc/store/queryHelper'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import queries from '@wsSrc/api/queries'

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
        async loadChildNodes({ getters, rootState }, nodeGroup) {
            const config = Worksheet.getters('activeRequestConfig')
            const queryEditorId = QueryEditor.getters('activeId')
            const { id: connId } = QueryConn.getters('activeQueryTabConn')
            const { data, completionItems } = await queryHelper.getNewTreeData({
                connId,
                nodeGroup,
                data: getters.dbTreeData,
                completionItems: rootState.prefAndStorage.identifier_auto_completion
                    ? []
                    : getters.completionItems,
                config,
            })
            QueryEditorTmp.update({
                where: queryEditorId,
                data(obj) {
                    obj.db_tree = data
                    if (!rootState.prefAndStorage.identifier_auto_completion)
                        obj.completion_items = completionItems
                },
            })
        },
        async fetchSchemas({ getters, rootState }) {
            const config = Worksheet.getters('activeRequestConfig')
            const queryEditorId = QueryEditor.getters('activeId')
            const { id, meta: { name: connection_name } = {} } = QueryConn.getters(
                'activeQueryTabConn'
            )

            QueryEditorTmp.update({
                where: queryEditorId,
                data: { loading_db_tree: true },
            })

            const [e, res] = await this.vue.$helpers.to(
                queries.post({ id, body: { sql: getters.schemaSql }, config })
            )
            if (e)
                QueryEditorTmp.update({
                    where: queryEditorId,
                    data: { loading_db_tree: false },
                })
            else {
                const { nodes, completionItems } = schemaNodeHelper.genNodeData({
                    queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]').safeObject,
                })
                if (nodes.length) {
                    let data = nodes
                    let completion_items = completionItems
                    const nodeGroupTypes = Object.values(
                        rootState.mxsWorkspace.config.NODE_GROUP_TYPES
                    )
                    // fetch expanded_nodes
                    for (const nodeGroup of getters.expandedNodes) {
                        if (nodeGroupTypes.includes(nodeGroup.type)) {
                            const {
                                data: newData,
                                completionItems: newCompletionItems,
                            } = await queryHelper.getNewTreeData({
                                connId: id,
                                nodeGroup,
                                data,
                                completionItems: rootState.prefAndStorage.identifier_auto_completion
                                    ? []
                                    : completion_items,
                            })
                            data = newData
                            if (!rootState.prefAndStorage.identifier_auto_completion)
                                completion_items = newCompletionItems
                        }
                    }
                    QueryEditorTmp.update({
                        where: queryEditorId,
                        data(obj) {
                            obj.loading_db_tree = false
                            obj.completion_items = completion_items
                            obj.db_tree_of_conn = connection_name
                            obj.db_tree = data
                        },
                    })
                }
            }
        },
    },
    getters: {
        // sidebar getters
        schemaSql: (state, getters, rootState) => {
            const { SYS_SCHEMAS, NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const schema = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            let sql = 'SELECT * FROM information_schema.SCHEMATA'
            if (!rootState.prefAndStorage.query_show_sys_schemas_flag)
                sql += ` WHERE ${schema} NOT IN(${SYS_SCHEMAS.map(db => `'${db}'`).join(',')})`
            sql += ` ORDER BY ${schema};`
            return sql
        },
        activeRecord: () => SchemaSidebar.find(QueryEditor.getters('activeId')) || {},
        expandedNodes: (state, getters) => getters.activeRecord.expanded_nodes || [],
        completionItems: () =>
            lodash.uniqBy(QueryEditor.getters('activeTmpRecord').completion_items || [], 'label'),
        dbTreeOfConn: () => QueryEditor.getters('activeTmpRecord').db_tree_of_conn || '',
        dbTreeData: () => QueryEditor.getters('activeTmpRecord').db_tree || {},
    },
}
