/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'
import queries from '@wsSrc/api/queries'
import { insertQueryEditor } from '@wsSrc/store/orm/initEntities'

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
            const entityIds = queryHelper
                .filterEntity(QueryEditor, payload)
                .map(entity => entity.id)

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
            const entityIds = queryHelper
                .filterEntity(QueryEditor, payload)
                .map(entity => entity.id)
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
         * Init QueryEditor entities if they aren't existed for
         * the active worksheet.
         */
        initQueryEditorEntities() {
            const wkeId = Worksheet.getters('getActiveWkeId')
            if (!QueryEditor.find(wkeId)) insertQueryEditor(wkeId)
            Worksheet.update({ where: wkeId, data: { query_editor_id: wkeId } })
        },
        /**
         * This calls action to populate schema-tree and change the wke name to
         * the connection name.
         */
        async handleInitialFetch({ dispatch }) {
            try {
                const { id: connId, meta: { name: connection_name } = {} } = QueryConn.getters(
                    'getActiveQueryTabConn'
                )
                const hasConnId = Boolean(connId)
                const isSchemaTreeEmpty = SchemaSidebar.getters('getDbTreeData').length === 0
                const hasSchemaTreeAlready =
                    SchemaSidebar.getters('getDbTreeOfConn') === connection_name
                if (hasConnId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await SchemaSidebar.dispatch('initialFetch')
                        Worksheet.update({
                            where: Worksheet.getters('getActiveWkeId'),
                            data: { name: connection_name },
                        })
                    }
                    if (Editor.getters('getIsDDLEditor'))
                        await dispatch('editorsMem/queryAlterTblSuppData', {}, { root: true })
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * This action is used to execute statement or statements.
         * Since users are allowed to modify the auto-generated SQL statement,
         * they can add more SQL statements after or before the auto-generated statement
         * which may receive error. As a result, the action log still log it as a failed action.
         * @param {String} payload.sql - sql to be executed
         * @param {String} payload.action - action name. e.g. DROP TABLE table_name
         * @param {Boolean} payload.showSnackbar - show successfully snackbar message
         */
        async exeStmtAction({ rootState, dispatch, commit }, { sql, action, showSnackbar = true }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id, meta: { name: connection_name } = {} } = QueryConn.getters(
                'getActiveQueryTabConn'
            )
            const queryEditorId = QueryEditor.getters('getQueryEditorId')
            const request_sent_time = new Date().valueOf()
            let stmt_err_msg_obj = {}
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id,
                    body: { sql, max_rows: rootState.prefAndStorage.query_row_limit },
                    config,
                })
            )
            if (e) this.vue.$logger.error(e)
            else {
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
                const errMsgs = results.filter(res => this.vue.$typy(res, 'errno').isDefined)
                // if multi statement mode, it'll still return only an err msg obj
                if (errMsgs.length) stmt_err_msg_obj = errMsgs[0]

                QueryEditorTmp.update({
                    where: queryEditorId,
                    data: {
                        exe_stmt_result: {
                            data: this.vue.$typy(res, 'data.data.attributes').safeObject,
                            stmt_err_msg_obj,
                        },
                    },
                })

                let queryAction
                if (!this.vue.$typy(stmt_err_msg_obj).isEmptyObject)
                    queryAction = this.vue.$mxs_t('errors.failedToExeAction', { action })
                else {
                    queryAction = this.vue.$mxs_t('success.exeAction', { action })
                    if (showSnackbar)
                        commit(
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            { text: [queryAction], type: 'success' },
                            { root: true }
                        )
                }
                dispatch(
                    'prefAndStorage/pushQueryLog',
                    {
                        startTime: request_sent_time,
                        name: queryAction,
                        sql,
                        res,
                        connection_name,
                        queryType: rootState.mxsWorkspace.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
    },
    getters: {
        getQueryEditorId: () => Worksheet.getters('getActiveWkeId'),
        getQueryEditor: (state, getters) => QueryEditor.find(getters.getQueryEditorId) || {},
        getActiveQueryTabId: (state, getters) => getters.getQueryEditor.active_query_tab_id,
        getQueryEditorTmp: (state, getters) => QueryEditorTmp.find(getters.getQueryEditorId) || {},
        getExeStmtResult: (state, getters) => getters.getQueryEditorTmp.exe_stmt_result || {},
    },
}
