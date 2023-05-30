/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import * as config from '@wsSrc/store/config'
import commonConfig from '@share/config'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'

export default {
    namespaced: true,
    state: {
        config: { ...config, COMMON_CONFIG: commonConfig },
        hidden_comp: [''],
        migr_dlg: { is_opened: false, etl_task_id: '', type: '' },
        etl_polling_interval: config.ETL_DEF_POLLING_INTERVAL,
        //Below states needed for the workspace package so it can be used in SkySQL
        is_conn_dlg_opened: false, // control showing connection dialog
    },
    mutations: {
        SET_HIDDEN_COMP(state, payload) {
            state.hidden_comp = payload
        },
        SET_MIGR_DLG(state, payload) {
            state.migr_dlg = payload
        },
        SET_ETL_POLLING_INTERVAL(state, payload) {
            state.etl_polling_interval = payload
        },
        SET_IS_CONN_DLG_OPENED(state, payload) {
            state.is_conn_dlg_opened = payload
        },
    },
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
    },
}
