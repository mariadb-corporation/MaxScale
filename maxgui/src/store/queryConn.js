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
import { pickBy } from 'utils/helpers'
import queryHelper from './queryHelper'
/**
 * @returns Initial connection related states
 */
export function connStatesToBeSynced() {
    return {
        active_sql_conn: {},
        conn_err_state: false,
    }
}

export default {
    namespaced: true,
    state: {
        sql_conns: {}, // persisted
        is_validating_conn: true,
        rc_target_names_map: {},
        pre_select_conn_rsrc: null,
        //states to be synced to worksheets_arr by calling sync_to_worksheets_arr
        ...connStatesToBeSynced(),
    },
    mutations: {
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
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
        // TODO: Create MutationCreator for mutation using mutate_sync_wke
        SET_ACTIVE_SQL_CONN(state, { queryState, payload, active_wke_id }) {
            queryHelper.mutate_sync_wke({
                scope: this,
                mutateStateModule: state,
                queryState,
                data: { active_sql_conn: payload },
                active_wke_id,
            })
        },
        SET_CONN_ERR_STATE(state, { queryState, payload, active_wke_id }) {
            queryHelper.mutate_sync_wke({
                scope: this,
                mutateStateModule: state,
                queryState,
                data: { conn_err_state: payload },
                active_wke_id,
            })
        },
        /**
         * When active_wke_id is changed, call this to sync states from worksheets_arr
         * back to sync states in this module
         * @param {Object} state - vuex state
         * @param {Object} wke - wke object
         */
        SYNC_CONN_STATES(state, wke) {
            queryHelper.mutateFlatStates({
                moduleState: state,
                data: pickBy(wke, (v, key) => Object.keys(connStatesToBeSynced()).includes(key)),
            })
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
                this.vue.$logger('store-query-fetchRcTargetNames').error(e)
            }
        },
        /**
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validatingConn(
            { state, commit, dispatch, rootState },
            { silentValidation = false } = {}
        ) {
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)
            try {
                const active_wke_id = rootState.query.active_wke_id
                const res = await this.$queryHttp.get(`/sql/`)
                const resConnMap = this.vue.$help.lodash.keyBy(res.data.data, 'id')
                const resConnIds = Object.keys(resConnMap)
                const clientConnIds = queryHelper.getClientConnIds()
                if (resConnIds.length === 0) {
                    dispatch('query/resetAllWkeStates', {}, { root: true })
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
                        dispatch('query/resetWkeStates', id, { root: true })
                    })

                    commit('SET_SQL_CONNS', validSqlConns)
                    // update active_sql_conn attributes
                    if (state.active_sql_conn.id) {
                        const active_sql_conn = validSqlConns[state.active_sql_conn.id]
                        commit('SET_ACTIVE_SQL_CONN', {
                            queryState: rootState.query,
                            payload: active_sql_conn,
                            active_wke_id,
                        })
                    }
                }
            } catch (e) {
                this.vue.$logger('store-query-validatingConn').error(e)
            }
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', false)
        },
        async openConnect({ dispatch, commit, rootState }, { body, resourceType }) {
            const active_wke_id = rootState.query.active_wke_id
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
                        queryState: rootState.query,
                        payload: active_sql_conn,
                        active_wke_id,
                    })

                    if (body.db) await dispatch('query/useDb', body.db, { root: true })
                    commit('SET_CONN_ERR_STATE', {
                        queryState: rootState.query,
                        payload: false,
                        active_wke_id,
                    })
                }
            } catch (e) {
                this.vue.$logger('store-query-openConnect').error(e)
                commit('SET_CONN_ERR_STATE', {
                    queryState: rootState.query,
                    payload: true,
                    active_wke_id,
                })
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
                this.vue.$logger('store-query-openBgConn').error(e)
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
                    dispatch('query/resetWkeStates', cnctId, { root: true })
                }
            } catch (e) {
                this.vue.$logger('store-query-disconnect').error(e)
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
                this.vue.$logger('store-query-deleteBgConn').error(e)
            }
        },
        async disconnectAll({ state, dispatch }) {
            try {
                for (const id of Object.keys(state.sql_conns))
                    await dispatch('disconnect', { showSnackbar: false, id })
            } catch (e) {
                this.vue.$logger('store-query-disconnectAll').error(e)
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
                    await dispatch('query/initialFetch', active_sql_conn, { root: true })
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
                this.vue.$logger('store-query-reconnect').error(e)
            }
        },
        clearConn({ commit, dispatch, state }) {
            try {
                const active_sql_conn = state.active_sql_conn
                commit('DELETE_SQL_CONN', active_sql_conn)
                dispatch('query/resetWkeStates', active_sql_conn.id, { root: true })
            } catch (e) {
                this.vue.$logger('store-query-clearConn').error(e)
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
    },
}
