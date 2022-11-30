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
import * as config from '@queryEditorSrc/store/config'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'
import WorksheetMem from '@queryEditorSrc/store/orm/models/WorksheetMem'

export default {
    namespaced: true,
    state: {
        config,
        hidden_comp: [''],
        axios_opts: {},
    },
    mutations: {
        SET_HIDDEN_COMP(state, payload) {
            state.hidden_comp = payload
        },
        SET_AXIOS_OPTS(state, payload) {
            state.axios_opts = payload
        },
    },
    actions: {
        createDefWorksheet() {
            if (Worksheet.all().length === 0) {
                const worksheet_id = this.vue.$helpers.uuidv1()
                const query_tab_id = this.vue.$helpers.uuidv1()
                Worksheet.create({
                    data: {
                        id: worksheet_id,
                        name: 'WORKSHEET',
                        schemaSidebar: new SchemaSidebar(),
                        queryConn: new QueryConn(),
                        worksheetMem: new WorksheetMem(),
                    },
                })
                QueryTab.create({
                    data: {
                        id: query_tab_id,
                        worksheet_id,
                        editor: new Editor(),
                        queryResult: new QueryResult(),
                        queryConn: new QueryConn(),
                        queryTabMem: new QueryTabMem(),
                    },
                })
                Worksheet.commit(state => (state.active_wke_id = worksheet_id))
                // update active_query_tab_map state
                QueryTab.commit(state => (state.active_query_tab_map[worksheet_id] = query_tab_id))
            }
        },
    },
}
