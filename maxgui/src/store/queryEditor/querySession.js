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
import allMemStatesModules from './allMemStatesModules'
import init, { defSessionState } from './initQueryEditorState'

export default {
    namespaced: true,
    state: {
        query_sessions: init.get_def_query_sessions, // persisted
        // each key holds a string value of the active session id of a worksheet
        active_session_by_wke_id_map: {}, // persisted
    },
    mutations: {
        ADD_NEW_SESSION(state, wke_id) {
            let name = 'Query Tab 1',
                count = 1
            const sessions_in_wke = state.query_sessions.filter(s => s.wke_id_fk === wke_id)
            if (sessions_in_wke.length) {
                const lastSession = sessions_in_wke[sessions_in_wke.length - 1]
                count = lastSession.count + 1
                name = `Query Tab ${count}`
            }
            const newSession = { ...defSessionState(wke_id), name, count }
            state.query_sessions.push(newSession)
        },
        DELETE_SESSION(state, id) {
            state.query_sessions = state.query_sessions.filter(s => s.id !== id)
        },
        UPDATE_SESSION(state, { idx, session }) {
            state.query_sessions = this.vue.$help.immutableUpdate(state.query_sessions, {
                [idx]: { $set: session },
            })
        },
        SET_ACTIVE_SESSION_BY_WKE_ID_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.active_session_by_wke_id_map, id)
            else
                state.active_session_by_wke_id_map = {
                    ...state.active_session_by_wke_id_map,
                    [id]: payload,
                }
        },
        /**
         * This mutation resets all sessions of the provided targetWke object
         * to its initial states except states that stores editor data
         * @param {Object} targetWke - target wke
         */
        REFRESH_SESSIONS_OF_A_WKE(state, targetWke) {
            const sessionIdxs = state.query_sessions.reduce((arr, s, idx) => {
                if (s.wke_id_fk === targetWke.id) arr.push(idx)
                return arr
            }, [])
            sessionIdxs.forEach(i => {
                const session = {
                    ...state.query_sessions[i],
                    ...queryHelper.syncStateCreator('queryConn'),
                    ...queryHelper.syncStateCreator('queryResult'),
                }
                state.query_sessions = this.vue.$help.immutableUpdate(state.query_sessions, {
                    [i]: { $set: session },
                })
            })
        },
        /**
         * This mutation resets all properties of the provided targetSession object
         * to its initial states except states that stores editor data
         * @param {Object} session - session to be reset
         */
        REFRESH_SESSION_OF_A_WKE(state, session) {
            const idx = state.query_sessions.indexOf(session)
            state.query_sessions = this.vue.$help.immutableUpdate(state.query_sessions, {
                [idx]: {
                    $set: {
                        ...state.query_sessions[idx],
                        ...queryHelper.syncStateCreator('queryConn'),
                        ...queryHelper.syncStateCreator('queryResult'),
                    },
                },
            })
        },
    },
    actions: {
        /**
         * This action add new session to the provided worksheet id.
         * First, it clones the connection of the current active worksheet then
         * add a new session, set it as the active session and finally set the clone connection as
         * the active connection.
         * @param {String} param.wke_id - worksheet id
         */
        async handleAddNewSession({ commit, state, dispatch, rootState, rootGetters }, wke_id) {
            try {
                const conn_to_be_cloned = rootGetters['queryConn/getWkeFirstSessConnByWkeId'](
                    wke_id
                )
                const active_db = rootState.schemaSidebar.active_db
                let sessionConn
                if (conn_to_be_cloned.id) {
                    await dispatch(
                        'queryConn/cloneConn',
                        {
                            conn_to_be_cloned,
                            binding_type: rootState.app_config.QUERY_CONN_BINDING_TYPES.SESSION,
                            getCloneObjRes: obj => (sessionConn = obj),
                        },
                        { root: true }
                    )
                    // also set the active db for the new session
                    if (active_db)
                        await this.$queryHttp.post(`/sql/${sessionConn.id}/queries`, {
                            sql: `USE ${this.vue.$help.escapeIdentifiers(active_db)};`,
                        })
                }

                commit('ADD_NEW_SESSION', wke_id)
                const newSessionId = state.query_sessions[state.query_sessions.length - 1].id
                commit('SET_ACTIVE_SESSION_BY_WKE_ID_MAP', {
                    id: wke_id,
                    payload: newSessionId,
                })

                if (sessionConn)
                    // Switch to use new clone connection
                    commit(
                        'queryConn/SET_ACTIVE_SQL_CONN',
                        {
                            payload: sessionConn,
                            id: newSessionId,
                        },
                        { root: true }
                    )
            } catch (e) {
                this.vue.$logger('store-querySession-handleAddNewSession').error(e)
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    { text: [this.i18n.t('errors.persistentStorage')], type: 'error' },
                    { root: true }
                )
            }
        },
        /**
         * If the session is bound to the default connection, only session object(tab) will be deleted.
         * The default connection should always be disconnected by action in the `conn-man-ctr` component
         * @param {Object} session - A session object
         */
        async handleDeleteSession({ commit, dispatch }, session) {
            const { id: conn_id, clone_of_conn_id = null } = session.active_sql_conn || {}
            if (!clone_of_conn_id) dispatch('releaseQueryModulesMem', session.id)
            //  Send request to delete the clone connection bound to this session and release memory
            else if (conn_id) {
                await dispatch('queryConn/disconnectClone', { id: conn_id }, { root: true })
                dispatch('resetSessionStates', { conn_id })
            }
            commit('DELETE_SESSION', session.id)
        },
        /**
         * Clear all session's data except active_sql_conn data
         * @param {Object} param.session - session to be cleared
         */
        handleClearTheLastSession({ state, commit, dispatch }, session) {
            const freshSession = {
                ...defSessionState(session.wke_id_fk),
                id: session.id,
                name: 'Query Tab 1',
                count: 1,
                active_sql_conn: session.active_sql_conn,
            }
            const idx = state.query_sessions.findIndex(s => s.id === session.id)
            commit('UPDATE_SESSION', { idx, session: freshSession })
            dispatch('releaseQueryModulesMem', session.id)
            dispatch('handleSyncSession', freshSession)
        },
        /**
         * @param {Object} param.session - session object to be sync to flat states
         */
        handleSyncSession({ commit }, session) {
            commit('queryConn/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
            commit('queryResult/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
            commit('editor/SYNC_WITH_PERSISTED_OBJ', session, { root: true })
        },
        /**
         * Release memory for target wke when delete a session
         * @param {String} param.session_id - session id.
         */
        releaseQueryModulesMem({ commit }, session_id) {
            Object.keys(allMemStatesModules).forEach(namespace => {
                // Only 'editor', 'queryConn', 'queryResult' modules have memStates keyed by session_id
                if (namespace !== 'schemaSidebar')
                    queryHelper.releaseMemory({
                        namespace,
                        commit,
                        id: session_id,
                        memStates: allMemStatesModules[namespace],
                    })
            })
        },
        /**
         * sessions cleanup
         * release memStates that uses session id as key,
         * refresh targetSession to its initial state. The target session
         * can be either found by using conn_id or session_id
         * @param {String} param.conn_id - connection id
         * @param {String} param.session_id - session id
         */
        resetSessionStates(
            { state, rootState, commit, dispatch, getters },
            { conn_id, session_id }
        ) {
            const targetSession = conn_id
                ? getters.getSessionByConnId(conn_id)
                : state.query_sessions.find(s => s.id === session_id)
            dispatch('releaseQueryModulesMem', targetSession.id)
            commit('REFRESH_SESSION_OF_A_WKE', targetSession)
            const freshSession = rootState.querySession.query_sessions.find(
                s => s.id === targetSession.id
            )
            dispatch('handleSyncSession', freshSession)
        },
    },
    getters: {
        getActiveSessionId: (state, getters, rootState) => {
            return state.active_session_by_wke_id_map[rootState.wke.active_wke_id]
        },
        getActiveSession: (state, getters) => {
            return state.query_sessions.find(s => s.id === getters.getActiveSessionId) || {}
        },
        getSessionsOfActiveWke: (state, getters, rootState) => {
            return state.query_sessions.filter(s => s.wke_id_fk === rootState.wke.active_wke_id)
        },
        getSessionsByWkeId: state => {
            return wke_id => state.query_sessions.filter(s => s.wke_id_fk === wke_id)
        },
        getSessionByConnId: state => {
            return conn_id =>
                state.query_sessions.find(
                    s => s.active_sql_conn && s.active_sql_conn.id === conn_id
                ) || {}
        },
    },
}
