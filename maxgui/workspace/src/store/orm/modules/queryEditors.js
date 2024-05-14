/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import { QUERY_TAB_TYPES } from '@wsSrc/constants'

export default {
    namespaced: true,
    actions: {
        /**
         * If there is a connection bound to the QueryEditor being deleted, it
         * disconnects it and all of its clones.
         * After that all entities related to the QueryEditor and itself will be purged.
         * @param {String|Function} payload
         */
        async cascadeDelete(_, payload) {
            const entityIds = QueryEditor.filterEntity(QueryEditor, payload).map(
                entity => entity.id
            )

            for (const id of entityIds) {
                const { id: connId } =
                    QueryConn.query()
                        .where('query_editor_id', id)
                        .first() || {}
                // delete the QueryEditor connection and its clones (query tabs)
                if (connId) await QueryConn.dispatch('cascadeDisconnect', { id: connId })
                // delete records in its relational tables
                QueryEditorTmp.delete(id)
                SchemaSidebar.delete(id)
                QueryTab.dispatch('cascadeDelete', t => t.query_editor_id === id)
                QueryEditor.delete(id) // delete itself
            }
        },
        /**
         * Refresh non-key and non-relational fields of an entity and its relations
         * @param {String|Function} payload -
         */
        cascadeRefresh(_, payload) {
            const entityIds = QueryEditor.filterEntity(QueryEditor, payload).map(
                entity => entity.id
            )
            entityIds.forEach(id => {
                // refresh its relations
                QueryEditorTmp.refresh(id)
                SchemaSidebar.refresh(id)
                // refresh all queryTabs and its relations
                QueryTab.dispatch('cascadeRefresh', t => t.query_editor_id === id)
                Worksheet.update({ where: id, data: { name: 'QUERY EDITOR' } })
                QueryEditor.refresh(id) // refresh itself
            })
        },
        /**
         * Insert a QueryEditor with its relational entities
         * @param {String} query_editor_id - QueryEditor id
         */
        insertQueryEditor(_, query_editor_id) {
            QueryEditor.insert({ data: { id: query_editor_id } })
            QueryEditorTmp.insert({ data: { id: query_editor_id } })
            SchemaSidebar.insert({ data: { id: query_editor_id } })
            QueryTab.dispatch('insertQueryTab', { query_editor_id })
        },
        /**
         * Init QueryEditor entities if they don't exist in the active worksheet.
         */
        initQueryEditorEntities({ dispatch }) {
            const wkeId = Worksheet.getters('activeId')
            if (!QueryEditor.find(wkeId)) dispatch('insertQueryEditor', wkeId)
            Worksheet.update({ where: wkeId, data: { query_editor_id: wkeId } })
        },
        /**
         * This calls action to populate schema-tree and change the wke name to
         * the connection name.
         */
        async handleInitialFetch({ getters, dispatch }) {
            try {
                const config = Worksheet.getters('activeRequestConfig')
                const { id: connId, meta: { name: connection_name } = {} } = QueryConn.getters(
                    'activeQueryTabConn'
                )
                const isSchemaTreeEmpty = SchemaSidebar.getters('dbTreeData').length === 0
                const hasSchemaTreeAlready =
                    SchemaSidebar.getters('dbTreeOfConn') === connection_name
                if (connId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await SchemaSidebar.dispatch('initialFetch')
                        Worksheet.update({
                            where: Worksheet.getters('activeId'),
                            data: { name: connection_name },
                        })
                    }
                    if (
                        this.vue.$typy(QueryTab.find(getters.activeQueryTabId), 'type')
                            .safeString === QUERY_TAB_TYPES.ALTER_EDITOR &&
                        !this.vue.$typy(AlterEditor.find(getters.activeQueryTabId), 'data')
                            .isEmptyObject
                    )
                        await dispatch(
                            'editorsMem/queryDdlEditorSuppData',
                            { connId, config },
                            { root: true }
                        )
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
    },
    getters: {
        activeId: () => Worksheet.getters('activeId'),
        activeRecord: (state, getters) => QueryEditor.find(getters.activeId) || {},
        activeQueryTabId: (state, getters) => getters.activeRecord.active_query_tab_id,
        activeTmpRecord: (state, getters) => QueryEditorTmp.find(getters.activeId) || {},
    },
}
