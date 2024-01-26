/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import queries from '@wsSrc/api/queries'
import { genSetMutations } from '@share/utils/helpers'
import {
    QUERY_CONN_BINDING_TYPES,
    QUERY_LOG_TYPES,
    ETL_DEF_POLLING_INTERVAL,
} from '@wsSrc/constants'

const states = () => ({
    hidden_comp: [''],
    migr_dlg: { is_opened: false, etl_task_id: '', type: '' },
    gen_erd_dlg: {
        is_opened: false,
        preselected_schemas: [],
        connection: null,
        gen_in_new_ws: false, // generate erd in a new worksheet
    },
    exec_sql_dlg: {
        is_opened: false,
        editor_height: 250,
        sql: '',
        /**
         * @property {object} data - Contains res.data.data.attributes of a query
         * @property {object} error
         */
        result: null,
        on_exec: () => null,
        after_cancel: () => null,
    },
    confirm_dlg: {
        is_opened: false,
        title: '',
        confirm_msg: '',
        save_text: 'save',
        cancel_text: 'dontSave',
        on_save: () => null,
        after_cancel: () => null,
    },
    etl_polling_interval: ETL_DEF_POLLING_INTERVAL,
    //Below states needed for the workspace package so it can be used in SkySQL
    conn_dlg: { is_opened: false, type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR },
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async initWorkspace({ dispatch }) {
            dispatch('initEntities')
            await dispatch('fileSysAccess/initStorage', {}, { root: true })
        },
        initEntities({ dispatch }) {
            if (Worksheet.all().length === 0) Worksheet.dispatch('insertBlankWke')
            else dispatch('initMemEntities')
        },
        /**
         * Initialize entities that will be kept only in memory for all worksheets and queryTabs
         */
        initMemEntities() {
            const worksheets = Worksheet.all()
            worksheets.forEach(w => {
                WorksheetTmp.insert({ data: { id: w.id } })
                if (w.query_editor_id) {
                    const queryEditor = QueryEditor.query()
                        .where('id', w.query_editor_id)
                        .with('queryTabs')
                        .first()
                    QueryEditorTmp.insert({ data: { id: queryEditor.id } })
                    queryEditor.queryTabs.forEach(t => QueryTabTmp.insert({ data: { id: t.id } }))
                } else if (w.etl_task_id) EtlTaskTmp.insert({ data: { id: w.etl_task_id } })
                else if (w.erd_task_id) ErdTaskTmp.insert({ data: { id: w.erd_task_id } })
            })
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
        async exeStmtAction(
            { state, rootState, dispatch, commit },
            { connId, sql, action, showSnackbar = true }
        ) {
            const config = Worksheet.getters('activeRequestConfig')
            const { meta: { name: connection_name } = {} } = QueryConn.find(connId)
            const request_sent_time = new Date().valueOf()
            let error = null
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: { sql, max_rows: rootState.prefAndStorage.query_row_limit },
                    config,
                })
            )
            if (e) this.vue.$logger.error(e)
            else {
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
                const errMsgs = results.filter(res => this.vue.$typy(res, 'errno').isDefined)
                // if multi statement mode, it'll still return only an err msg obj
                if (errMsgs.length) error = errMsgs[0]
                commit('SET_EXEC_SQL_DLG', {
                    ...state.exec_sql_dlg,
                    result: {
                        data: this.vue.$typy(res, 'data.data.attributes').safeObject,
                        error,
                    },
                })
                let queryAction
                if (error) queryAction = this.vue.$mxs_t('errors.failedToExeAction', { action })
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
                        queryType: QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
        /**
         *
         * @param {string} param.connId - connection id
         * @param {boolean} [param.isCreating] - is creating a new table
         * @param {string} [param.schema] - schema name
         * @param {string} [param.name] - table name
         * @param {string} [param.actionName] - action name
         * @param {function} param.successCb - success callback function
         */
        async exeDdlScript(
            { state, dispatch, getters },
            { connId, isCreating = false, schema, name, successCb, actionName = '' }
        ) {
            const { quotingIdentifier: quoting } = this.vue.$helpers
            let action
            if (actionName) action = actionName
            else {
                const targetObj = `${quoting(schema)}.${quoting(name)}`
                action = `Apply changes to ${targetObj}`
                if (isCreating) action = `Create ${targetObj}`
            }

            await dispatch('exeStmtAction', { connId, sql: state.exec_sql_dlg.sql, action })
            if (!getters.isExecFailed) await this.vue.$typy(successCb).safeFunction()
        },
    },
    getters: {
        execSqlDlgResult: state => state.exec_sql_dlg.result,
        getExecErr: (state, getters) => {
            const { error } = getters.execSqlDlgResult || {}
            return error
        },
        isExecFailed: (state, getters) => {
            if (getters.execSqlDlgResult) return Boolean(getters.getExecErr)
            return false
        },
    },
}
