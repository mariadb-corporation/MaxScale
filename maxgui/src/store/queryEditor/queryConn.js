/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'

const statesToBeSynced = queryHelper.syncStateCreator('queryConn')
const memStates = queryHelper.memStateCreator('queryConn')

export default {
    namespaced: true,
    state: {
        sql_conns: {}, // persisted
        is_validating_conn: true,
        conn_err_state: false,
        rc_target_names_map: {},
        pre_select_conn_rsrc: null,
        ...memStates,
        ...statesToBeSynced,
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'querySession.query_sessions',
        }),
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },
        SET_SQL_CONNS(state, payload) {
            state.sql_conns = payload
        },
        ADD_SQL_CONN(state, payload) {
            this.vue.$set(state.sql_conns, payload.id, payload)
        },
        DELETE_SQL_CONN(state, payload) {
            this.vue.$delete(state.sql_conns, payload.id)
        },
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_PRE_SELECT_CONN_RSRC(state, payload) {
            state.pre_select_conn_rsrc = payload
        },
    },
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            try {
                const res = await this.$queryHttp.get(`/${resourceType}?fields[${resourceType}]=id`)
                if (res.data.data) {
                    const names = res.data.data.map(({ id, type }) => ({ id, type }))
                    commit('SET_RC_TARGET_NAMES_MAP', {
                        ...state.rc_target_names_map,
                        [resourceType]: names,
                    })
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-fetchRcTargetNames').error(e)
            }
        },
        /**
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validatingConn(
            { state, commit, dispatch, rootGetters },
            { silentValidation = false } = {}
        ) {
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)
            try {
                const active_session_id = rootGetters['querySession/getActiveSessionId']
                const res = await this.$queryHttp.get(`/sql/`)
                const resConnMap = this.vue.$help.lodash.keyBy(res.data.data, 'id')
                const resConnIds = Object.keys(resConnMap)
                const clientConnIds = queryHelper.getClientConnIds()
                if (resConnIds.length === 0) {
                    dispatch('resetAllWkeStates')
                    commit('SET_SQL_CONNS', {})
                } else {
                    const validConnIds = clientConnIds.filter(id => resConnIds.includes(id))
                    const validSqlConns = Object.keys(state.sql_conns)
                        .filter(id => validConnIds.includes(id))
                        .reduce(
                            (acc, id) => ({
                                ...acc,
                                [id]: {
                                    ...state.sql_conns[id],
                                    attributes: resConnMap[id].attributes, // update attributes
                                },
                            }),
                            {}
                        )
                    const invalidCnctIds = Object.keys(state.sql_conns).filter(
                        id => !(id in validSqlConns)
                    )
                    //deleteInvalidConn
                    invalidCnctIds.forEach(id => {
                        this.vue.$help.deleteCookie(`conn_id_body_${id}`)
                        dispatch('resetWkeStates', id)
                    })

                    commit('SET_SQL_CONNS', validSqlConns)
                    // update active_sql_conn attributes
                    if (state.active_sql_conn.id) {
                        const active_sql_conn = validSqlConns[state.active_sql_conn.id]
                        commit('SET_ACTIVE_SQL_CONN', {
                            payload: active_sql_conn,
                            id: active_session_id,
                        })
                    }
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-validatingConn').error(e)
            }
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', false)
        },
        async openConnect({ dispatch, commit, rootState, rootGetters }, { body, resourceType }) {
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                const res = await this.$queryHttp.post(`/sql?persist=yes&max-age=86400`, body)
                if (res.status === 201) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('info.connSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    const connId = res.data.data.id
                    const active_sql_conn = {
                        id: connId,
                        attributes: res.data.data.attributes,
                        name: body.target,
                        type: resourceType,
                        binding_type: rootState.app_config.QUERY_CONN_BINDING_TYPES.WORKSHEET,
                    }
                    commit('ADD_SQL_CONN', active_sql_conn)
                    commit('SET_ACTIVE_SQL_CONN', {
                        payload: active_sql_conn,
                        id: active_session_id,
                    })

                    if (body.db) await dispatch('schemaSidebar/useDb', body.db, { root: true })
                    commit('SET_CONN_ERR_STATE', false)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-openConnect').error(e)
                commit('SET_CONN_ERR_STATE', true)
            }
        },
        /**
         *  Clone a connection to allow it run in the background
         * @param {Object} conn_to_be_cloned - connection to be cloned
         */
        async openBgConn({ commit, rootState }, conn_to_be_cloned) {
            try {
                const res = await this.$queryHttp.post(
                    `/sql/${conn_to_be_cloned.id}/clone?persist=yes&max-age=86400`
                )
                if (res.status === 201) {
                    const connId = res.data.data.id
                    const conn = {
                        id: connId,
                        attributes: res.data.data.attributes,
                        name: conn_to_be_cloned.name,
                        type: conn_to_be_cloned.type,
                        binding_type: rootState.app_config.QUERY_CONN_BINDING_TYPES.BACKGROUND,
                    }
                    commit('ADD_SQL_CONN', conn)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-openBgConn').error(e)
            }
        },
        async disconnect({ state, commit, dispatch }, { showSnackbar, id: cnctId }) {
            try {
                const res = await this.$queryHttp.delete(`/sql/${cnctId}`)
                if (res.status === 204) {
                    if (showSnackbar)
                        commit(
                            'SET_SNACK_BAR_MESSAGE',
                            {
                                text: [this.i18n.t('info.disconnSuccessfully')],
                                type: 'success',
                            },
                            { root: true }
                        )
                    commit('DELETE_SQL_CONN', state.sql_conns[cnctId])
                    dispatch('resetWkeStates', cnctId)
                }
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnect').error(e)
            }
        },
        /**
         * Disconnect the "BACKGROUND" connection of the current active_sql_conn
         * @param {Object} active_sql_conn - active_sql_conn
         */
        async disconnectBgConn({ dispatch, rootState, state }, active_sql_conn) {
            try {
                // find BACKGROUND connections of the current active sql connection
                const bgCnns = Object.values(state.sql_conns).filter(
                    cnn =>
                        cnn.name === active_sql_conn.name &&
                        cnn.binding_type ===
                            rootState.app_config.QUERY_CONN_BINDING_TYPES.BACKGROUND
                )
                for (const conn of bgCnns) await dispatch('disconnect', { id: conn.id })
            } catch (e) {
                this.vue.$logger('store-queryConn-deleteBgConn').error(e)
            }
        },
        async disconnectAll({ state, dispatch }) {
            try {
                for (const id of Object.keys(state.sql_conns))
                    await dispatch('disconnect', { showSnackbar: false, id })
            } catch (e) {
                this.vue.$logger('store-queryConn-disconnectAll').error(e)
            }
        },
        async reconnect({ state, commit, dispatch }) {
            const active_sql_conn = state.active_sql_conn
            try {
                const res = await this.$queryHttp.post(`/sql/${active_sql_conn.id}/reconnect`)
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('info.reconnSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await dispatch('schemaSidebar/initialFetch', active_sql_conn, {
                        root: true,
                    })
                } else
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('errors.reconnFailed')],
                            type: 'error',
                        },
                        { root: true }
                    )
                await dispatch('validatingConn', { silentValidation: true })
            } catch (e) {
                this.vue.$logger('store-queryConn-reconnect').error(e)
            }
        },
        clearConn({ commit, dispatch, state }) {
            try {
                const active_sql_conn = state.active_sql_conn
                commit('DELETE_SQL_CONN', active_sql_conn)
                dispatch('resetWkeStates', active_sql_conn.id)
            } catch (e) {
                this.vue.$logger('store-queryConn-clearConn').error(e)
            }
        },
        /**
         * Call this action when disconnect a connection to
         * clear the state of the worksheet having that connection to its initial state
         */
        resetWkeStates({ state, commit, rootState, dispatch }, cnctId) {
            const targetSession = rootState.querySession.query_sessions.find(
                s => this.vue.$typy(s, 'active_sql_conn.id').safeString === cnctId
            )
            const targetWke = rootState.wke.worksheets_arr.find(
                w => w.id === this.vue.$typy(targetSession, 'wke_id_fk').safeString
            )

            if (targetWke) {
                dispatch('wke/releaseQueryModulesMem', targetWke.id, { root: true })
                commit('wke/REFRESH_WKE', targetWke, { root: true })
                commit('querySession/REFRESH_SESSIONS_OF_A_WKE', targetWke, { root: true })
                /**
                 * if connection id to be deleted is equal to current connected
                 * resource of active worksheet, sync wke states to flat states
                 */
                if (state.active_sql_conn.id === cnctId) {
                    const freshWke = rootState.wke.worksheets_arr.find(
                        wke => wke.id === targetWke.id
                    )
                    const freshSession = rootState.querySession.query_sessions.find(
                        s => s.id === targetSession.id
                    )
                    dispatch('wke/handleSyncWke', freshWke, { root: true })
                    dispatch('querySession/handleSyncSession', freshSession, { root: true })
                }
            }
        },
        // Reset all when there is no active connections
        resetAllWkeStates({ rootState, commit }) {
            for (const targetWke of rootState.wke.worksheets_arr) {
                commit('wke/REFRESH_WKE', targetWke, { root: true })
                commit('querySession/REFRESH_SESSIONS_OF_A_WKE', targetWke, { root: true })
            }
        },
    },
    getters: {
        getBgConn: (state, getters, rootState) => {
            const bgConns = Object.values(state.sql_conns).filter(
                conn =>
                    conn.name === state.active_sql_conn.name &&
                    conn.binding_type === rootState.app_config.QUERY_CONN_BINDING_TYPES.BACKGROUND
            )
            if (bgConns.length) return bgConns[0]
            return {}
        },
        getIsConnBusy: (state, getters, rootState, rootGetters) => {
            const { value = false } =
                state.is_conn_busy_map[rootGetters['querySession/getActiveSessionId']] || {}
            return value
        },
        getLostCnnErrMsgObj: (state, getters, rootState, rootGetters) => {
            const { value = {} } =
                state.lost_cnn_err_msg_obj_map[rootGetters['querySession/getActiveSessionId']] || {}
            return value
        },
        getBoundConnByWkeId: (state, getters, rootState) => {
            return wke_id => {
                const sessions_in_wke = rootState.querySession.query_sessions.filter(
                    s => s.wke_id_fk === wke_id
                )
                let wkeBoundConn = {}
                for (const s of sessions_in_wke)
                    if (s.active_sql_conn.id) {
                        wkeBoundConn = s.active_sql_conn
                        break
                    }
                return wkeBoundConn
            }
        },
    },
}
