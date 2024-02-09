/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'
import allMemStatesModules from './allMemStatesModules'
import init, { defWorksheetState } from './initQueryEditorState'

const def_worksheets_arr = init.get_def_worksheets_arr
export default {
    namespaced: true,
    state: {
        // Toolbar states
        is_fullscreen: false,
        // worksheet states
        worksheets_arr: def_worksheets_arr, // persisted
        active_wke_id: def_worksheets_arr[0].id, // persisted
    },
    mutations: {
        //Toolbar mutations
        SET_FULLSCREEN(state, payload) {
            state.is_fullscreen = payload
        },
        // worksheet mutations
        ADD_NEW_WKE(state) {
            state.worksheets_arr.push(defWorksheetState())
        },
        DELETE_WKE(state, id) {
            state.worksheets_arr = state.worksheets_arr.filter(wke => wke.id !== id)
        },
        UPDATE_WKE(state, { idx, wke }) {
            state.worksheets_arr = this.vue.$helpers.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
        SET_ACTIVE_WKE_ID(state, payload) {
            state.active_wke_id = payload
        },
        /**
         * This mutation resets all properties of the provided targetWke object to its initial states
         * except states that stores editor data
         * @param {Object} targetWke - wke to be reset
         */
        REFRESH_WKE(state, targetWke) {
            const idx = state.worksheets_arr.indexOf(targetWke)
            const wke = {
                ...targetWke,
                ...queryHelper.syncStateCreator('schemaSidebar'),
                name: 'WORKSHEET',
            }
            state.worksheets_arr = this.vue.$helpers.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
    },
    actions: {
        /**
         * This calls action to populate schema-tree and change the wke name to
         * the connection name.
         */
        async handleInitialFetch({ dispatch, rootState, rootGetters }) {
            try {
                const { id: conn_id, name: conn_name } = rootState.queryConn.active_sql_conn || {}
                const hasConnId = conn_id
                const isSchemaTreeEmpty = rootGetters['schemaSidebar/getDbTreeData'].length === 0
                const hasSchemaTreeAlready =
                    this.vue.$typy(rootGetters['schemaSidebar/getCurrDbTree'], 'data_of_conn')
                        .safeString === conn_name
                if (hasConnId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await dispatch('schemaSidebar/initialFetch', {}, { root: true })
                        dispatch('changeWkeName', conn_name)
                    }
                    if (rootGetters['editor/getIsDDLEditor'])
                        await dispatch('editor/queryAlterTblSuppData', {}, { root: true })
                }
            } catch (e) {
                this.vue.$logger('store-wke-handleInitialFetch').error(e)
            }
        },
        async addNewWs({ commit, state, dispatch }) {
            try {
                commit('ADD_NEW_WKE')
                const new_active_wke_id = state.worksheets_arr[state.worksheets_arr.length - 1].id
                commit('SET_ACTIVE_WKE_ID', new_active_wke_id)
                await dispatch(
                    'querySession/handleAddNewSession',
                    { wke_id: new_active_wke_id },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger('store-wke-addNewWs').error(e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        async handleDeleteWke({ commit, dispatch, rootGetters }, id) {
            try {
                // release module memory states
                dispatch('releaseQueryModulesMem', id)

                const sessions = rootGetters['querySession/getSessionsByWkeId'](id)
                const { id: wkeConnId = '' } = rootGetters['queryConn/getWkeConnByWkeId'](id)
                // First call queryConn/disconnect to delete the wke connection and its clones (session tabs)
                if (wkeConnId)
                    await dispatch('queryConn/disconnect', { id: wkeConnId }, { root: true })
                // delete session objects
                for (const session of sessions)
                    await dispatch('querySession/handleDeleteSession', session, { root: true })
                commit('DELETE_WKE', id)
                // remove the key
                commit('querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP', { id }, { root: true })
            } catch (e) {
                this.vue.$logger('store-wke-handleDeleteWke').error(e)
            }
        },
        /**
         * @param {Object} param.wke - worksheet object to be sync to flat states
         */
        handleSyncWke({ commit }, wke) {
            commit('schemaSidebar/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
        },
        /**
         * Release memory for target wke when delete a worksheet or disconnect a
         * connection from a worksheet
         * @param {String} param.wke_id - worksheet id.
         */
        releaseQueryModulesMem({ commit }, wke_id) {
            //release memory only for schemaSidebar here.
            Object.keys(allMemStatesModules).forEach(namespace => {
                if (namespace === 'schemaSidebar')
                    queryHelper.releaseMemory({
                        namespace,
                        commit,
                        id: wke_id,
                        memStates: allMemStatesModules[namespace],
                    })
            })
        },
        /**
         * wke cleanup
         * release memStates that uses wke id as key,
         * refresh wke state to its initial state.
         * Call this function when the disconnect action is called
         * @param {String} wkeConnId - id of the connection has binding_type === WORKSHEET
         */
        resetWkeStates({ commit, rootState, dispatch, getters }, wkeConnId) {
            const wkeConn = rootState.queryConn.sql_conns[wkeConnId]
            const targetWke = getters.getWkeById(wkeConn.wke_id_fk)
            if (targetWke) {
                dispatch('releaseQueryModulesMem', targetWke.id)
                commit('REFRESH_WKE', targetWke)
                const freshWke = rootState.wke.worksheets_arr.find(wke => wke.id === targetWke.id)
                dispatch('handleSyncWke', freshWke)
            }
        },
        changeWkeName({ commit, rootState, getters }, name) {
            let newWke = this.vue.$helpers.lodash.cloneDeep(getters.getActiveWke)
            newWke.name = name
            commit('UPDATE_WKE', {
                idx: rootState.wke.worksheets_arr.indexOf(getters.getActiveWke),
                wke: newWke,
            })
        },
    },
    getters: {
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id) || {}
        },
        getWkeById: state => {
            return id => state.worksheets_arr.find(w => w.id === id) || {}
        },
    },
}
