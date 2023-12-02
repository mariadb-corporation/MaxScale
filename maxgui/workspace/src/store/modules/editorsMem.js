/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queries from '@wsSrc/api/queries'

export default {
    namespaced: true,
    state: {
        selected_query_txt: '',
        is_max_rows_valid: true,
        // states for ALTER_EDITOR
        charset_collation_map: {},
        def_db_charset_map: {},
        engines: [],
    },
    mutations: {
        SET_SELECTED_QUERY_TXT(state, payload) {
            state.selected_query_txt = payload
        },
        SET_IS_MAX_ROWS_VALID(state, payload) {
            state.is_max_rows_valid = payload
        },
        SET_CHARSET_COLLATION_MAP(state, payload) {
            state.charset_collation_map = payload
        },
        SET_DEF_DB_CHARSET_MAP(state, payload) {
            state.def_db_charset_map = payload
        },
        SET_ENGINES(state, payload) {
            state.engines = payload
        },
    },
    actions: {
        async queryCharsetCollationMap({ commit }, { connId, config }) {
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: {
                        sql:
                            // eslint-disable-next-line vue/max-len
                            'SELECT character_set_name, collation_name, is_default FROM information_schema.collations',
                    },
                    config,
                })
            )
            if (!e) {
                let charsetCollationMap = {}
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const charset = row[0]
                    const collation = row[1]
                    const isDefCollation = row[2] === 'Yes'
                    let charsetObj = charsetCollationMap[`${charset}`] || {
                        collations: [],
                    }
                    if (isDefCollation) charsetObj.defCollation = collation
                    charsetObj.collations.push(collation)
                    charsetCollationMap[charset] = charsetObj
                })
                commit('SET_CHARSET_COLLATION_MAP', charsetCollationMap)
            }
        },
        async queryDefDbCharsetMap({ commit }, { connId, config }) {
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: {
                        sql:
                            // eslint-disable-next-line vue/max-len
                            'SELECT schema_name, default_character_set_name FROM information_schema.schemata',
                    },
                    config,
                })
            )
            if (!e) {
                let defDbCharsetMap = {}
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const schema_name = row[0]
                    const default_character_set_name = row[1]
                    defDbCharsetMap[schema_name] = default_character_set_name
                })
                commit('SET_DEF_DB_CHARSET_MAP', defDbCharsetMap)
            }
        },
        async queryEngines({ commit, rootState }, { connId, config }) {
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: { sql: 'SELECT engine FROM information_schema.ENGINES' },
                    config,
                })
            )
            if (!e)
                commit(
                    'SET_ENGINES',
                    this.vue.$helpers.lodash.xorWith(
                        this.vue
                            .$typy(res, 'data.data.attributes.results[0].data')
                            .safeArray.flat(),
                        rootState.mxsWorkspace.config.UNSUPPORTED_TBL_CREATION_ENGINES
                    )
                )
        },
        /**
         * @param {string} param.connId - connection id
         * @param {object} param.config - axios config
         */
        async queryDdlEditorSuppData({ state, dispatch }, param) {
            if (param.connId && param.config) {
                if (this.vue.$typy(state.engines).isEmptyArray)
                    await dispatch('queryEngines', param)
                if (this.vue.$typy(state.charset_collation_map).isEmptyObject)
                    await dispatch('queryCharsetCollationMap', param)
                if (this.vue.$typy(state.def_db_charset_map).isEmptyObject)
                    await dispatch('queryDefDbCharsetMap', param)
            }
        },
    },
}
