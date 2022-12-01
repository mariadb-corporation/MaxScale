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
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import allMemStatesModules from '@queryEditorSrc/store/allMemStatesModules'
import init, { defWorksheetState } from '@queryEditorSrc/store/initQueryEditorState'

const def_worksheets_arr = init.get_def_worksheets_arr
export default {
    namespaced: true,
    state: {
        // worksheet states
        worksheets_arr: def_worksheets_arr, // persisted
        active_wke_id: def_worksheets_arr[0].id, // persisted
    },
    mutations: {
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
        async handleInitialFetch({ dispatch, rootGetters }) {
            try {
                const { id: connId, name: connName } = rootGetters[
                    'queryConns/getActiveQueryTabConn'
                ]
                const hasConnId = Boolean(connId)
                const isSchemaTreeEmpty = rootGetters['schemaSidebar/getDbTreeData'].length === 0
                const hasSchemaTreeAlready =
                    this.vue.$typy(rootGetters['schemaSidebar/getCurrDbTree'], 'data_of_conn')
                        .safeString === connName
                if (hasConnId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await dispatch('schemaSidebar/initialFetch', {}, { root: true })
                        dispatch('changeWkeName', connName)
                    }
                    if (rootGetters['editor/getIsDDLEditor'])
                        await dispatch('editor/queryAlterTblSuppData', {}, { root: true })
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async addNewWs({ commit, state, dispatch }) {
            try {
                commit('ADD_NEW_WKE')
                const new_active_wke_id = state.worksheets_arr[state.worksheets_arr.length - 1].id
                commit('SET_ACTIVE_WKE_ID', new_active_wke_id)
                await dispatch(
                    'queryTab/handleAddNewQueryTab',
                    { wke_id: new_active_wke_id },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger.error(e)
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

                const queryTabs = rootGetters['queryTab/getQueryTabsByWkeId'](id)
                const { id: wkeConnId = '' } = rootGetters['queryConns/getWkeConnByWkeId'](id)
                // First call queryConns/disconnect to delete the wke connection and its clones (query tabs)
                if (wkeConnId)
                    await dispatch('queryConns/disconnect', { id: wkeConnId }, { root: true })
                // delete queryTab objects
                for (const queryTab of queryTabs)
                    await dispatch('queryTab/handleDeleteQueryTab', queryTab, { root: true })
                commit('DELETE_WKE', id)
                // remove the key
                commit('queryTab/SET_ACTIVE_QUERY_TAB_MAP', { id }, { root: true })
            } catch (e) {
                this.vue.$logger.error(e)
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
            const wkeConn = QueryConn.find(wkeConnId) || {}
            const targetWke = getters.getWkeById(wkeConn.worksheet_id)
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
        getActiveWkeId: (state, getters, rootState) => {
            const {
                ORM_NAMESPACE,
                ORM_PERSISTENT_ENTITIES: { WORKSHEETS },
            } = rootState.queryEditorConfig.config
            const { active_wke_id } = rootState[ORM_NAMESPACE][WORKSHEETS] || {}
            return active_wke_id
        },
    },
}
