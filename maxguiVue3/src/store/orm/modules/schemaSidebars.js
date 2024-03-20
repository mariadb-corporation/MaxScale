/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import queries from '@/api/sql/queries'
import { NODE_TYPES, NODE_NAME_KEYS, SYS_SCHEMAS } from '@/constants/workspace'

export default {
  namespaced: true,
  actions: {
    async initialFetch({ dispatch }) {
      await dispatch('fetchSchemas')
      await QueryConn.dispatch('updateActiveDb')
    },
    async fetchSchemas({ getters }) {
      const config = Worksheet.getters('activeRequestConfig')
      const queryEditorId = QueryEditor.getters('activeId')
      const { id, meta: { name: connection_name } = {} } = QueryConn.getters('activeQueryTabConn')

      QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: true } })

      const [e, res] = await this.vue.$helpers.tryAsync(
        queries.post({ id, body: { sql: getters.schemaSql }, config })
      )
      if (e) QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: false } })
      else {
        const nodes = schemaNodeHelper.genNodes({
          queryResult: this.vue.$typy(res, 'data.data.attributes.results[0]').safeObject,
        })
        if (nodes.length)
          QueryEditorTmp.update({
            where: queryEditorId,
            data(obj) {
              obj.loading_db_tree = false
              obj.db_tree_of_conn = connection_name
              obj.db_tree = nodes
            },
          })
        else QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: false } })
      }
    },
  },
  getters: {
    // sidebar getters
    schemaSql: (state, getters, rootState) => {
      const schema = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
      let sql = 'SELECT * FROM information_schema.SCHEMATA'
      if (!rootState.prefAndStorage.query_show_sys_schemas_flag)
        sql += ` WHERE ${schema} NOT IN(${SYS_SCHEMAS.map((db) => `'${db}'`).join(',')})`
      sql += ` ORDER BY ${schema};`
      return sql
    },
    activeRecord: () => SchemaSidebar.find(QueryEditor.getters('activeId')) || {},
    expandedNodes: (state, getters) => getters.activeRecord.expanded_nodes || [],
    dbTreeOfConn: () => QueryEditor.getters('activeTmpRecord').db_tree_of_conn || '',
    dbTreeData: () => QueryEditor.getters('activeTmpRecord').db_tree || [],
    schemaTree: (state, getters, rootState) => {
      let tree = getters.dbTreeData
      const activeSchema = QueryConn.getters('activeSchema')
      if (rootState.prefAndStorage.identifier_auto_completion && activeSchema)
        return tree.filter((n) => n.qualified_name !== activeSchema)
      return tree
    },
    schemaTreeCompletionItems: (state, getters) =>
      schemaNodeHelper.genNodeCompletionItems(getters.schemaTree),
    activeSchemaIdentifierCompletionItems: () => {
      const { schema_identifier_names_completion_items = [] } =
        QueryTabTmp.find(QueryEditor.getters('activeQueryTabId')) || {}
      return schema_identifier_names_completion_items
    },
    activeCompletionItems: (state, getters, rootState, rootGetters) => {
      return [
        ...getters.schemaTreeCompletionItems,
        ...getters.activeSchemaIdentifierCompletionItems,
        ...rootGetters['prefAndStorage/snippetCompletionItems'],
      ]
    },
  },
}
