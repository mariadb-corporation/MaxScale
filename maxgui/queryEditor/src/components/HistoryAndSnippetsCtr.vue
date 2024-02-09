<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <v-tabs
                v-model="activeView"
                hide-slider
                :height="20"
                class="v-tabs--query-editor-style"
            >
                <v-tab
                    :key="SQL_QUERY_MODES.HISTORY"
                    :href="`#${SQL_QUERY_MODES.HISTORY}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $mxs_t('history') }}
                </v-tab>
                <v-tab
                    :key="SQL_QUERY_MODES.SNIPPETS"
                    :href="`#${SQL_QUERY_MODES.SNIPPETS}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $mxs_t('snippets') }}
                </v-tab>
            </v-tabs>
        </div>
        <keep-alive>
            <template v-if="persistedQueryData.length">
                <table-list
                    v-if="
                        activeView === SQL_QUERY_MODES.HISTORY ||
                            activeView === SQL_QUERY_MODES.SNIPPETS
                    "
                    :key="activeView"
                    :height="dynDim.height - headerHeight"
                    :width="dynDim.width"
                    :headers="headers"
                    :rows="currRows"
                    showSelect
                    showGroupBy
                    groupBy="date"
                    :menuOpts="menuOpts"
                    :showEditBtn="activeView === SQL_QUERY_MODES.SNIPPETS"
                    :defExportFileName="
                        `MaxScale Query ${
                            activeView === SQL_QUERY_MODES.HISTORY ? 'History' : 'Snippets'
                        }`
                    "
                    @on-delete-selected="handleDeleteSelectedRows"
                    @on-done-editing="onDoneEditingSnippets"
                    v-on="$listeners"
                >
                    <template v-slot:header-connection_name="{ data: { maxWidth, activatorID } }">
                        <mxs-truncate-str
                            :tooltipItem="{ txt: 'Connection Name', activatorID }"
                            :maxWidth="maxWidth"
                        />
                    </template>
                    <template v-if="activeView === SQL_QUERY_MODES.SNIPPETS" v-slot:header-name>
                        {{ $mxs_t('prefix') }}
                    </template>
                    <template
                        v-slot:date="{ data: { cell, maxWidth, activatorID, isDragging, search } }"
                    >
                        <mxs-truncate-str
                            :key="cell"
                            v-mxs-highlighter="{ keyword: search, txt: formatDate(cell) }"
                            :disabled="isDragging"
                            :tooltipItem="{
                                txt: `${formatDate(cell)}`,
                                activatorID,
                            }"
                            :maxWidth="maxWidth"
                        />
                    </template>
                    <template v-slot:action="{ data: { cell, maxWidth, isDragging, search } }">
                        <!-- TODO: Make a global tooltip for showing action column -->
                        <v-tooltip
                            :key="cell.name"
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop mxs-color-helper white text-navigation pa-2 pb-4"
                            :disabled="isDragging"
                        >
                            <template v-slot:activator="{ on }">
                                <span
                                    v-mxs-highlighter="{ keyword: search, txt: cell.name }"
                                    class="d-inline-block text-truncate"
                                    :style="{ maxWidth: `${maxWidth}px` }"
                                    v-on="on"
                                >
                                    {{ cell.name }}
                                </span>
                            </template>
                            <table class="action-table-tooltip px-1">
                                <caption class="text-left font-weight-bold mb-3 pl-1">
                                    {{
                                        $mxs_t('queryResInfo')
                                    }}
                                    <v-divider class="mxs-color-helper border-separator" />
                                </caption>

                                <tr v-for="(value, key) in cell" :key="`${key}`">
                                    <template v-if="key !== 'type'">
                                        <td>
                                            {{ key }}
                                        </td>
                                        <td
                                            :class="{
                                                'text-truncate': key !== 'response',
                                            }"
                                            :style="{
                                                maxWidth: '600px',
                                                whiteSpace:
                                                    key !== 'response' ? 'nowrap' : 'pre-line',
                                            }"
                                        >
                                            {{ value }}
                                        </td>
                                    </template>
                                </tr>
                            </table>
                        </v-tooltip>
                    </template>
                    <template
                        v-if="activeView === SQL_QUERY_MODES.HISTORY"
                        v-slot:left-table-tools-append
                    >
                        <div class="ml-2">
                            <mxs-filter-list
                                v-model="selectedLogTypes"
                                selectAllOnActivated
                                :label="$mxs_t('logTypes')"
                                :items="queryLogTypes"
                                returnObject
                                :maxHeight="200"
                            />
                        </div>
                    </template>
                </table-list>
            </template>
            <i18n
                v-else
                :path="
                    activeView === SQL_QUERY_MODES.HISTORY
                        ? 'mxs.historyTabGuide'
                        : 'mxs.snippetTabGuide'
                "
                class="d-flex align-center"
                tag="span"
            >
                <!-- Slots for SQL_QUERY_MODES.SNIPPETS only -->
                <template v-slot:shortcut>
                    &nbsp;<b>{{ OS_KEY }} + S</b>&nbsp;
                </template>
                <template v-slot:icon>
                    &nbsp;
                    <v-icon color="accent-dark" size="16">mdi-star-plus-outline</v-icon>
                    &nbsp;
                </template>
            </i18n>
        </keep-alive>
        <mxs-conf-dlg
            v-model="isConfDlgOpened"
            :title="
                activeView === SQL_QUERY_MODES.HISTORY
                    ? $mxs_t('clearSelectedQueries', {
                          targetType: $mxs_t('queryHistory'),
                      })
                    : $mxs_t('deleteSnippets')
            "
            saveText="delete"
            minBodyWidth="624px"
            :onSave="deleteSelectedRows"
        >
            <template v-slot:confirm-text>
                <p>
                    {{
                        $mxs_t('info.clearSelectedQueries', {
                            quantity:
                                itemsToBeDeleted.length === rows.length
                                    ? $mxs_t('entire')
                                    : $mxs_t('selected'),
                            targetType: $mxs_t(
                                activeView === SQL_QUERY_MODES.HISTORY ? 'queryHistory' : 'snippets'
                            ),
                        })
                    }}
                </p>
            </template>
        </mxs-conf-dlg>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapState, mapMutations, mapGetters } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'history-and-snippets-ctr',
    components: { 'table-list': ResultDataTable },
    props: {
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
    },
    data() {
        return {
            headerHeight: 0,
            itemsToBeDeleted: [],
            selectedLogTypes: [],
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            OS_KEY: state => state.queryEditorConfig.config.OS_KEY,
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            QUERY_LOG_TYPES: state => state.queryEditorConfig.config.QUERY_LOG_TYPES,
            SQL_RES_TBL_CTX_OPT_TYPES: state =>
                state.queryEditorConfig.config.SQL_RES_TBL_CTX_OPT_TYPES,
            curr_query_mode: state => state.queryResult.curr_query_mode,
            query_history: state => state.queryPersisted.query_history,
            query_snippets: state => state.queryPersisted.query_snippets,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (
                    this.curr_query_mode === this.SQL_QUERY_MODES.HISTORY ||
                    this.curr_query_mode === this.SQL_QUERY_MODES.SNIPPETS
                )
                    this.SET_CURR_QUERY_MODE({ payload: value, id: this.getActiveSessionId })
            },
        },
        queryLogTypes() {
            return Object.values(this.QUERY_LOG_TYPES).map(type => ({ text: type }))
        },
        headers() {
            let data = []
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY:
                    data = this.query_history
                    break
                case this.SQL_QUERY_MODES.SNIPPETS:
                    data = this.query_snippets
            }
            return Object.keys(this.$typy(data[0]).safeObjectOrEmpty).map(field => {
                let header = {
                    text: field,
                    capitalize: true,
                }
                // assign default width to each column to have better view
                switch (field) {
                    case 'date':
                        header.width = 150
                        header.customGroup = data => {
                            const { rows, idx } = data
                            let map = new Map()
                            rows.forEach(row => {
                                const key = this.$helpers.dateFormat({
                                    moment: this.$moment,
                                    value: row[idx],
                                    formatType: 'ddd, DD MMM YYYY',
                                })
                                let matrix = map.get(key) || [] // assign an empty arr if not found
                                matrix.push(row)
                                map.set(key, matrix)
                            })
                            return map
                        }
                        header.filter = (value, search) =>
                            this.$helpers.ciStrIncludes(
                                this.$helpers.dateFormat({
                                    moment: this.$moment,
                                    value,
                                    formatType: 'ddd, DD MMM YYYY',
                                }),
                                search
                            )
                        break
                    case 'connection_name':
                        header.width = 215
                        break
                    case 'time':
                        header.width = 90
                        header.groupable = false
                        break
                    case 'action':
                        header.groupable = false
                        header.filter = (value, search) =>
                            this.$helpers.ciStrIncludes(JSON.stringify(value), search)
                        break
                    case 'name':
                        header.width = 240
                        if (this.activeView === this.SQL_QUERY_MODES.SNIPPETS)
                            header.editableCol = true
                        break
                    case 'sql':
                        if (this.activeView === this.SQL_QUERY_MODES.SNIPPETS)
                            header.editableCol = true
                }
                return header
            })
        },
        persistedQueryData() {
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY:
                    return this.query_history
                case this.SQL_QUERY_MODES.SNIPPETS:
                    return this.query_snippets
                default:
                    return []
            }
        },
        rows() {
            return this.persistedQueryData.map(item => Object.values(item))
        },
        currRows() {
            let data = this.persistedQueryData
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY: {
                    const types = this.selectedLogTypes.map(log => log.text)
                    data = data.filter(log => {
                        return types.includes(log.action.type)
                    })
                    break
                }
            }
            return data.map(item => Object.values(item))
        },
        menuOpts() {
            const {
                CLIPBOARD,
                TXT_EDITOR: { INSERT },
            } = this.SQL_RES_TBL_CTX_OPT_TYPES
            return [
                {
                    text: this.$mxs_t('copyToClipboard'),
                    children: [
                        {
                            text: 'SQL',
                            type: CLIPBOARD,
                            action: ({ opt, data }) => this.txtOptHandler({ opt, data }),
                        },
                    ],
                },
                {
                    text: this.$mxs_t('placeToEditor'),
                    children: [
                        {
                            text: 'SQL',
                            type: INSERT,
                            action: ({ opt, data }) => this.txtOptHandler({ opt, data }),
                        },
                    ],
                },
            ]
        },
    },
    activated() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_HISTORY: 'queryPersisted/SET_QUERY_HISTORY',
            SET_QUERY_SNIPPETS: 'queryPersisted/SET_QUERY_SNIPPETS',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        handleDeleteSelectedRows(itemsToBeDeleted) {
            this.itemsToBeDeleted = itemsToBeDeleted
            this.isConfDlgOpened = true
        },
        deleteSelectedRows() {
            const { cloneDeep, xorWith, isEqual } = this.$helpers.lodash
            let targetMatrices = cloneDeep(this.itemsToBeDeleted).map(
                row => row.filter((_, i) => i !== 0) // Remove # col
            )
            const newMaxtrices = xorWith(this.rows, targetMatrices, isEqual)
            // Convert to array of objects
            const newData = this.$helpers.getObjectRows({
                columns: this.headers.map(h => h.text),
                rows: newMaxtrices,
            })

            this[`SET_QUERY_${this.activeView}`](newData)
        },
        txtOptHandler({ opt, data }) {
            let rowData = this.$helpers.getObjectRows({
                columns: this.headers.map(h => h.text),
                rows: [data.row.filter((_, i) => i !== 0)], // Remove # col
            })
            let sql, name
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY: {
                    name = rowData[0].action.name
                    sql = rowData[0].action.sql
                    break
                }
                case this.SQL_QUERY_MODES.SNIPPETS:
                    sql = rowData[0].sql
            }
            const {
                TXT_EDITOR: { INSERT },
                CLIPBOARD,
            } = this.SQL_RES_TBL_CTX_OPT_TYPES
            // if no name is defined when storing the query, sql query is stored to name
            let sqlTxt = sql ? sql : name
            switch (opt.type) {
                case CLIPBOARD:
                    this.$helpers.copyTextToClipboard(sqlTxt)
                    break
                case INSERT:
                    this.$emit('place-to-editor', sqlTxt)
                    break
            }
        },
        onDoneEditingSnippets(changedCells) {
            const { cloneDeep, isEqual } = this.$helpers.lodash
            let cells = cloneDeep(changedCells)
            let snippets = cloneDeep(this.query_snippets)
            cells.forEach(c => {
                delete c.objRow['#'] // Remove # col
                const idxOfRow = this.query_snippets.findIndex(item => isEqual(item, c.objRow))
                if (idxOfRow > -1)
                    snippets[idxOfRow] = { ...snippets[idxOfRow], [c.colName]: c.value }
            })
            this.SET_QUERY_SNIPPETS(snippets)
        },
        formatDate(cell) {
            return this.$helpers.dateFormat({
                moment: this.$moment,
                value: cell,
                formatType: 'ddd, DD MMM YYYY',
            })
        },
    },
}
</script>

<style lang="scss" scoped>
.action-table-tooltip {
    border-spacing: 0;
    td {
        font-size: 0.875rem;
        color: $navigation;
        height: 24px;
        vertical-align: middle;
        &:first-of-type {
            padding-right: 16px;
        }
    }
}
</style>
