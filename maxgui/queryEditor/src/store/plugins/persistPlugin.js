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
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'

export default new VuexPersistence({
    key: 'query-editor',
    storage: localForage,
    asyncStorage: true,
    reducer: state => ({
        queryEditorORM: state.queryEditorORM,
        queryPersisted: state.queryPersisted,
        //TODO: remove below fields once ORM is completely added
        wke: {
            worksheets_arr: state.wke.worksheets_arr,
            active_wke_id: state.wke.active_wke_id,
        },
        queryConn: { sql_conns: state.queryConn.sql_conns },
        queryTab: {
            query_tabs: state.queryTab.query_tabs,
            active_query_tab_map: state.queryTab.active_query_tab_map,
        },
    }),
}).plugin
