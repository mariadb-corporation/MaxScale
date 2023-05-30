/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import queries from '@wsSrc/api/queries'
import TableParser from '@wsSrc/utils/TableParser'

const parser = new TableParser()

export default {
    namespaced: true,
    state: {
        selected_query_txt: '',
        file_dlg_data: {
            is_opened: false,
            title: '',
            confirm_msg: '',
            on_save: () => null,
            dont_save: () => null,
        },
        is_max_rows_valid: true,
        // states for DDL_EDITOR
        charset_collation_map: {},
        def_db_charset_map: {},
        engines: [],
        parsed_ddl: {}, // key is the schema name, value is the parsed data of tables
    },
    mutations: {
        SET_SELECTED_QUERY_TXT(state, payload) {
            state.selected_query_txt = payload
        },
        SET_FILE_DLG_DATA(state, payload) {
            state.file_dlg_data = payload
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
        SET_PARSED_DDL(state, payload) {
            state.parsed_ddl = payload
        },
    },
    actions: {
        async queryCharsetCollationMap({ commit }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
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
        async queryDefDbCharsetMap({ commit }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
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
        async queryEngines({ commit }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
            const [e, res] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: {
                        sql: 'SELECT engine FROM information_schema.ENGINES',
                    },
                    config,
                })
            )
            if (!e) commit('SET_ENGINES', res.data.data.attributes.results[0].data.flat())
        },
        async queryAlterTblSuppData({ state, dispatch }) {
            if (this.vue.$typy(state.engines).isEmptyArray) await dispatch('queryEngines')
            if (this.vue.$typy(state.charset_collation_map).isEmptyObject)
                await dispatch('queryCharsetCollationMap')
            if (this.vue.$typy(state.def_db_charset_map).isEmptyObject)
                await dispatch('queryDefDbCharsetMap')
        },
        async enableSqlQuoteShowCreate({ rootState }, connId) {
            await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: {
                        sql: 'SET SESSION sql_quote_show_create = 1',
                        max_rows: rootState.prefAndStorage.query_row_limit,
                    },
                    config: Worksheet.getters('getActiveRequestConfig'),
                })
            )
        },
        /**
         * @param {string} param.connId - id of connection
         * @param {string[]} param.tableNodes - tables to be queried and parsed
         */
        async queryAndParseDDL({ dispatch, commit, rootState }, { connId, tableNodes }) {
            const { $helpers, $typy } = this.vue
            await dispatch('enableSqlQuoteShowCreate', connId)
            let sql = tableNodes.map(node => `SHOW CREATE TABLE ${node.qualified_name};`).join('\n')
            const [, res] = await $helpers.to(
                queries.post({
                    id: connId,
                    body: {
                        sql,
                        max_rows: rootState.prefAndStorage.query_row_limit,
                    },
                    config: Worksheet.getters('getActiveRequestConfig'),
                })
            )

            const parsedDDL = $typy(res, 'data.data.attributes.results').safeArray.reduce(
                (acc, item, i) => {
                    const schema = tableNodes[i].schema
                    if (!acc[schema]) acc[schema] = []
                    const parsed = parser.parse($typy(item, 'data[0][1]').safeString)
                    acc[schema].push(parsed)
                    return acc
                },
                {}
            )
            commit('SET_PARSED_DDL', parsedDDL)
        },
    },
}
