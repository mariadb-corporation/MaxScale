<template>
    <v-progress-linear v-if="isFetching" indeterminate />
    <keep-alive v-else>
        <mxs-sql-editor
            v-if="activeSpec === specs.DDL"
            :value="ddlData"
            readOnly
            class="pl-2 fill-height"
            :options="{ contextmenu: false, fontSize: 14 }"
        />
        <result-data-table
            v-else-if="$typy(specData, 'fields').isDefined"
            :key="activeSpec"
            :height="dim.height"
            :width="dim.width"
            :headers="headers"
            :data="rows"
            :hasInsertOpt="false"
            showGroupBy
        >
            <template v-slot:right-table-tools-prepend>
                <mxs-tooltip-btn
                    btnClass="reload-schemas mr-2"
                    x-small
                    outlined
                    depressed
                    :disabled="isFetching"
                    color="primary"
                    @click="fetchAll"
                >
                    <template v-slot:btn-content>
                        <v-icon size="14">$vuetify.icons.mxs_reload </v-icon>
                    </template>
                    {{ $mxs_t('reload') }}
                </mxs-tooltip-btn>
            </template>
        </result-data-table>
        <div v-else>
            <div v-for="(v, key) in specData" :key="key">
                <b>{{ key }}:</b>
                <span class="d-inline-block ml-4">{{ v }}</span>
            </div>
        </div>
    </keep-alive>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { mapState, mapMutations } from 'vuex'
import Worksheet from '@wsModels/Worksheet'
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import queries from '@wsSrc/api/queries'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'

export default {
    name: 'insight-viewer-tab-item',
    components: { ResultDataTable },
    props: {
        dim: { type: Object, required: true },
        conn: { type: Object, required: true },
        node: { type: Object, required: true },
        activeSpec: { type: String, required: true },
        specs: { type: Object, required: true },
        nodeType: { type: String, required: true },
    },
    data() {
        return {
            analyzedData: {},
            isFetching: true,
        }
    },
    computed: {
        ...mapState({
            INSIGHT_SPECS: state => state.mxsWorkspace.config.INSIGHT_SPECS,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
        }),
        specData() {
            return this.$typy(this.analyzedData, `[${this.activeSpec}]`).safeObject
        },
        isSchemaNode() {
            return this.nodeType === this.NODE_TYPES.SCHEMA
        },
        schemaName() {
            return schemaNodeHelper.getSchemaName(this.node)
        },
        specQueryMap() {
            const { escapeSingleQuote, quotingIdentifier } = this.$helpers
            const { qualified_name } = this.node

            const nodeIdentifier = quotingIdentifier(this.node.name)
            const nodeLiteralStr = `'${escapeSingleQuote(this.node.name)}'`

            const schemaIdentifier = quotingIdentifier(this.schemaName)
            const schemaLiteralStr = `'${escapeSingleQuote(this.schemaName)}'`

            const {
                CREATION_INFO,
                DDL,
                TABLES,
                VIEWS,
                COLUMNS,
                INDEXES,
                TRIGGERS,
                SP,
                FN,
            } = this.INSIGHT_SPECS
            return Object.values(this.specs).reduce((map, spec) => {
                switch (spec) {
                    case CREATION_INFO:
                    case DDL:
                        if (this.nodeType === this.NODE_TYPES.TRIGGER)
                            map[spec] = `SHOW CREATE ${this.nodeType} ${nodeIdentifier}`
                        else map[spec] = `SHOW CREATE ${this.nodeType} ${qualified_name}`
                        break
                    case TABLES:
                    case VIEWS:
                        map[spec] = `SHOW TABLE STATUS FROM ${qualified_name} WHERE Comment ${
                            spec === TABLES ? '<>' : '='
                        } 'VIEW'`
                        break
                    case COLUMNS:
                    case INDEXES: {
                        let tbl =
                            spec === COLUMNS
                                ? 'INFORMATION_SCHEMA.COLUMNS'
                                : 'INFORMATION_SCHEMA.STATISTICS'
                        let query = `SELECT * FROM ${tbl} WHERE TABLE_SCHEMA = ${schemaLiteralStr}`
                        if (!this.isSchemaNode) query += ` AND TABLE_NAME = ${nodeLiteralStr}`
                        map[spec] = query
                        break
                    }
                    case TRIGGERS: {
                        let query = `SHOW TRIGGERS FROM ${schemaIdentifier}`
                        if (!this.isSchemaNode) query += ` WHERE \`Table\` = ${nodeLiteralStr}`
                        map[spec] = query
                        break
                    }
                    case SP:
                    case FN:
                        map[spec] = `SHOW ${
                            spec === SP ? 'PROCEDURE' : 'FUNCTION'
                        } STATUS WHERE Db = ${schemaLiteralStr}`
                        break
                }
                return map
            }, {})
        },
        isFilteredSpec() {
            return Object.keys(this.excludedColumnsBySpec).includes(this.activeSpec)
        },
        headers() {
            return this.$typy(this.specData, 'fields').safeArray.map(field => {
                let h = { text: field }
                if (
                    this.isFilteredSpec &&
                    this.excludedColumnsBySpec[this.activeSpec].includes(field)
                )
                    h.hidden = true
                return h
            })
        },
        ddlData() {
            const { VIEW, TRIGGER, SP, FN } = this.NODE_TYPES
            let ddl = ''
            switch (this.nodeType) {
                case TRIGGER:
                case SP:
                case FN:
                    ddl = this.$typy(this.analyzedData, `[${this.activeSpec}]data[0][2]`).safeString
                    break
                case VIEW:
                default:
                    ddl = this.$typy(this.analyzedData, `[${this.activeSpec}]data[0][1]`).safeString
            }
            return this.$helpers.formatSQL(ddl)
        },
        rows() {
            return this.$typy(this.specData, 'data').safeArray
        },
        excludedColumnsBySpec() {
            const { COLUMNS, INDEXES, TRIGGERS, SP, FN } = this.INSIGHT_SPECS
            const specs = [COLUMNS, INDEXES, TRIGGERS, SP, FN]
            let cols = ['TABLE_CATALOG', 'TABLE_SCHEMA']
            if (!this.isSchemaNode) cols.push('TABLE_NAME')
            return specs.reduce((map, spec) => {
                switch (spec) {
                    case INDEXES:
                        map[spec] = [...cols, 'INDEX_SCHEMA']
                        break
                    case TRIGGERS:
                        map[spec] = this.isSchemaNode ? [] : ['Table']
                        break
                    case SP:
                    case FN:
                        map[spec] = ['Db', 'Type']
                        break
                    default:
                        map[spec] = cols
                }
                return map
            }, {})
        },
    },
    watch: {
        activeSpec: {
            immediate: true,
            async handler(v) {
                if (!this.analyzedData[v]) await this.fetch(v)
            },
        },
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        async fetch(spec) {
            const sql = this.specQueryMap[spec]
            if (sql) {
                this.isFetching = true
                const result = await this.query(sql)
                this.$set(this.analyzedData, spec, result)
            }
            this.isFetching = false
        },
        async query(sql) {
            const { getErrorsArr } = this.$helpers
            const [e, res] = await this.$helpers.to(
                queries.post({
                    id: this.conn.id,
                    body: { sql },
                    config: Worksheet.getters('activeRequestConfig'),
                })
            )
            if (e) this.SET_SNACK_BAR_MESSAGE({ text: getErrorsArr(e), type: 'error' })
            return this.$typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty
        },
        async fetchAll() {
            for (const spec of Object.values(this.specs)) await this.fetch(spec)
        },
    },
}
</script>
