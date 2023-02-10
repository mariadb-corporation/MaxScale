/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import * as config from '@wsSrc/store/config'
import commonConfig from '@share/config'
import initEntities from '@wsSrc/store/orm/initEntities'

export default {
    namespaced: true,
    state: {
        config: { ...config, COMMON_CONFIG: commonConfig },
        hidden_comp: [''],
        migr_dlg: { is_opened: false, etl_task_id: '', type: '' },
        etl_polling_interval: config.ETL_DEF_POLLING_INTERVAL,
        //Below states needed for the workspace package so it can be used in SkySQL
        is_conn_dlg_opened: false, // control showing connection dialog
        /* SkySQL has multiple maxscales, so this state keeps the connections of
         * a maxscale. It'll be used for comparing the result of `/sql` endpoint.
         */
        conns_to_be_validated: [],
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
        SET_CONNS_TO_BE_VALIDATED(state, payload) {
            state.conns_to_be_validated = payload
        },
    },
    actions: {
        async initWorkspace({ dispatch }) {
            initEntities()
            await dispatch('fileSysAccess/initStorage', {}, { root: true })
        },
    },
}
