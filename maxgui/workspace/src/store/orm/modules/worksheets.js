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
import Worksheet from '@wsModels/Worksheet'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import QueryConn from '@wsModels/QueryConn'
import Editor from '@wsModels/Editor'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import queryHelper from '@wsSrc/store/queryHelper'
import { query } from '@wsSrc/api/query'

export default {
    namespaced: true,
    state: {
        active_wke_id: null, // Persistence
    },
    actions: {
        /**
         * If a record is deleted, then the corresponding records in its relational
         * tables will be automatically deleted
         * @param {String|Function} payload - either a worksheet id or a callback function that return Boolean (filter)
         */
        cascadeDelete(_, payload) {
            const entityIds = queryHelper.filterEntity(Worksheet, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                Worksheet.delete(id) // delete itself
                const { active_query_tab_id } = Worksheet.find(id) || {}
                if (active_query_tab_id) {
                    // delete records in its relational tables
                    QueryEditorTmp.delete(id)
                    SchemaSidebar.delete(id)
                    QueryConn.delete(c => c.worksheet_id === id)
                    QueryTab.dispatch('cascadeDelete', t => t.worksheet_id === id)
                }
            })
        },
        /**
         * Refresh non-key and non-relational fields of an entity and its relations
         * @param {String|Function} payload - either a Worksheet id or a callback function that return Boolean (filter)
         */
        cascadeRefresh(_, payload) {
            const entityIds = queryHelper.filterEntity(Worksheet, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                Worksheet.refresh(id) // refresh itself
                // refresh its relations
                QueryEditorTmp.refresh(id)
                SchemaSidebar.refresh(id)
                // refresh all queryTabs and its relations
                QueryTab.dispatch('cascadeRefresh', t => t.worksheet_id === id)
            })
        },
        /**
         * This calls action to populate schema-tree and change the wke name to
         * the connection name.
         */
        async handleInitialFetch({ dispatch }) {
            try {
                const { id: connId, name: connName } = QueryConn.getters('getActiveQueryTabConn')
                const hasConnId = Boolean(connId)
                const isSchemaTreeEmpty = SchemaSidebar.getters('getDbTreeData').length === 0
                const hasSchemaTreeAlready = SchemaSidebar.getters('getDbTreeOfConn') === connName
                if (hasConnId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await SchemaSidebar.dispatch('initialFetch')
                        dispatch('changeWkeName', connName)
                    }
                    if (Editor.getters('getIsDDLEditor'))
                        await dispatch('editorsMem/queryAlterTblSuppData', {}, { root: true })
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * If there is a connection bound to the worksheet being deleted, it
         * disconnects the connection bound to the worksheet and its cloned connections.
         * After that all entities related to the worksheet and itself will be purged.
         * @param {String} id - worksheet_id
         */
        async handleDeleteWke({ dispatch }, id) {
            const { id: wkeConnId = '' } = QueryConn.getters('getWkeConnByWkeId')(id)
            // delete the wke connection and its clones (query tabs)
            if (wkeConnId) await QueryConn.dispatch('cascadeDisconnectWkeConn', { id: wkeConnId })
            dispatch('cascadeDelete', id)
        },
        changeWkeName({ getters }, name) {
            Worksheet.update({ where: getters.getActiveWkeId, data: { name } })
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
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')
            const activeWkeId = Worksheet.getters('getActiveWkeId')
            const request_sent_time = new Date().valueOf()
            let stmt_err_msg_obj = {}
            const [e, res] = await this.vue.$helpers.to(
                query({
                    id: activeQueryTabConn.id,
                    body: { sql, max_rows: rootState.prefAndStorage.query_row_limit },
                })
            )
            if (e) this.vue.$logger.error(e)
            else {
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
                const errMsgs = results.filter(res => this.vue.$typy(res, 'errno').isDefined)
                // if multi statement mode, it'll still return only an err msg obj
                if (errMsgs.length) stmt_err_msg_obj = errMsgs[0]

                QueryEditorTmp.update({
                    where: activeWkeId,
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
                        connection_name: activeQueryTabConn.name,
                        queryType: rootState.mxsWorkspace.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
    },
    getters: {
        getActiveWkeId: state => state.active_wke_id,
        getActiveWke: (state, getters) => Worksheet.find(getters.getActiveWkeId) || {},
        getActiveQueryTabId: () => Worksheet.getters('getActiveWke').active_query_tab_id,
        getWorksheetMem: () => QueryEditorTmp.find(Worksheet.getters('getActiveWkeId')) || {},
        getExeStmtResult: (state, getters) => getters.getWorksheetMem.exe_stmt_result || {},
    },
}
