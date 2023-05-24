/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'

export default new VuexPersistence({
    key: 'query-editor',
    storage: localForage,
    asyncStorage: true,
    reducer: state => ({
        queryPersisted: state.queryPersisted,
        wke: {
            worksheets_arr: state.wke.worksheets_arr,
            active_wke_id: state.wke.active_wke_id,
        },
        queryConn: { sql_conns: state.queryConn.sql_conns },
        querySession: {
            query_sessions: state.querySession.query_sessions,
            active_session_by_wke_id_map: state.querySession.active_session_by_wke_id_map,
        },
    }),
}).plugin
