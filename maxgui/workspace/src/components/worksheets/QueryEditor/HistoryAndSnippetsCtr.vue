<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <v-tabs
                v-model="activeMode"
                hide-slider
                :height="20"
                class="v-tabs--mxs-workspace-style"
            >
                <v-tab
                    :key="QUERY_MODES.HISTORY"
                    :href="`#${QUERY_MODES.HISTORY}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $mxs_t('history') }}
                </v-tab>
                <v-tab
                    :key="QUERY_MODES.SNIPPETS"
                    :href="`#${QUERY_MODES.SNIPPETS}`"
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
                    v-if="activeMode === QUERY_MODES.HISTORY || activeMode === QUERY_MODES.SNIPPETS"
                    :key="activeMode"
                    :height="dim.height - headerHeight"
                    :width="dim.width"
                    :headers="headers"
                    :data="currRows"
                    showSelect
                    showGroupBy
                    :groupByColIdx="idxOfDateCol"
                    :menuOpts="menuOpts"
                    :showEditBtn="activeMode === QUERY_MODES.SNIPPETS"
                    :defExportFileName="
                        `MaxScale Query ${
                            activeMode === QUERY_MODES.HISTORY ? 'History' : 'Snippets'
                        }`
                    "
                    :exportAsSQL="false"
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
                    <template v-if="activeMode === QUERY_MODES.SNIPPETS" v-slot:header-name>
                        {{ $mxs_t('prefix') }}
                    </template>
                    <template
                        v-slot:date="{
                            on,
                            highlighterData,
                            data: { cell },
                        }"
                    >
                        <span
                            v-mxs-highlighter="{ ...highlighterData, txt: formatDate(cell) }"
                            class="text-truncate"
                            v-on="on"
                        >
                            {{ formatDate(cell) }}
                        </span>
                    </template>
                    <template v-slot:action="{ on, highlighterData, data: { cell, activatorID } }">
                        <div
                            v-mxs-highlighter="{ ...highlighterData, txt: cell.name }"
                            class="text-truncate"
                            v-on="on"
                            @mouseenter="actionCellData = { data: cell, activatorID }"
                            @mouseleave="actionCellData = null"
                        >
                            {{ cell.name }}
                        </div>
                    </template>
                    <template
                        v-if="activeMode === QUERY_MODES.HISTORY"
                        v-slot:left-table-tools-append
                    >
                        <div class="ml-2">
                            <mxs-filter-list
                                v-model="logTypesToShow"
                                :label="$mxs_t('logTypes')"
                                :items="queryLogTypes"
                                :maxHeight="200"
                                hideSelectAll
                                hideSearch
                            />
                        </div>
                    </template>
                </table-list>
            </template>
            <i18n
                v-else
                :path="
                    activeMode === QUERY_MODES.HISTORY
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
                    <v-icon color="primary" size="16">mdi-star-plus-outline</v-icon>
                    &nbsp;
                </template>
            </i18n>
        </keep-alive>
        <mxs-dlg
            v-model="isConfDlgOpened"
            :title="
                activeMode === QUERY_MODES.HISTORY
                    ? $mxs_t('clearSelectedQueries', {
                          targetType: $mxs_t('queryHistory'),
                      })
                    : $mxs_t('deleteSnippets')
            "
            saveText="delete"
            minBodyWidth="624px"
            :onSave="deleteSelectedRows"
        >
            <template v-slot:form-body>
                <p>
                    {{
                        $mxs_t('info.clearSelectedQueries', {
                            quantity:
                                itemsToBeDeleted.length === rows.length
                                    ? $mxs_t('entire')
                                    : $mxs_t('selected'),
                            targetType: $mxs_t(
                                activeMode === QUERY_MODES.HISTORY ? 'queryHistory' : 'snippets'
                            ),
                        })
                    }}
                </p>
            </template>
        </mxs-dlg>
        <v-tooltip
            v-if="$typy(actionCellData, 'activatorID').safeString"
            :value="Boolean(actionCellData)"
            top
            transition="slide-y-transition"
            :activator="`#${actionCellData.activatorID}`"
        >
            <table class="action-table-tooltip px-1">
                <caption class="text-left font-weight-bold mb-3 pl-1">
                    {{
                        $mxs_t('queryResInfo')
                    }}
                    <v-divider class="mxs-color-helper border-separator" />
                </caption>

                <tr v-for="(value, key) in actionCellData.data" :key="`${key}`">
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
                                whiteSpace: key !== 'response' ? 'nowrap' : 'pre-line',
                            }"
                        >
                            {{ value }}
                        </td>
                    </template>
                </tr>
            </table>
        </v-tooltip>
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
import { mapState, mapMutations } from 'vuex'
import QueryResult from '@wsModels/QueryResult'
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import { QUERY_MODES, NODE_CTX_TYPES, QUERY_LOG_TYPES, OS_KEY } from '@wsSrc/constants'

export default {
    name: 'history-and-snippets-ctr',
    components: { 'table-list': ResultDataTable },
    props: {
        dim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
        queryMode: { type: String, required: true },
        queryTabId: { type: String, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            itemsToBeDeleted: [],
            logTypesToShow: [],
            isConfDlgOpened: false,
            actionCellData: null,
        }
    },
    computed: {
        ...mapState({
            query_history: state => state.prefAndStorage.query_history,
            query_snippets: state => state.prefAndStorage.query_snippets,
        }),
        activeMode: {
            get() {
                return this.queryMode
            },
            set(v) {
                if (
                    this.queryMode === this.QUERY_MODES.HISTORY ||
                    this.queryMode === this.QUERY_MODES.SNIPPETS
                )
                    QueryResult.update({ where: this.queryTabId, data: { query_mode: v } })
            },
        },
        queryLogTypes() {
            return Object.values(QUERY_LOG_TYPES)
        },
        dateFormatType() {
            return 'E, dd MMM yyyy'
        },
        headers() {
            let data = []
            switch (this.activeMode) {
                case this.QUERY_MODES.HISTORY:
                    data = this.query_history
                    break
                case this.QUERY_MODES.SNIPPETS:
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
                        header.useCellSlot = true
                        header.dateFormatType = this.dateFormatType
                        break
                    case 'connection_name':
                        header.width = 215
                        break
                    case 'time':
                        header.width = 90
                        break
                    case 'action':
                        header.useCellSlot = true
                        header.valuePath = 'name'
                        break
                    // Fields for QUERY_MODES.SNIPPETS
                    case 'name':
                        header.width = 240
                        header.editableCol = true
                        break
                    case 'sql':
                        header.editableCol = true
                }
                return header
            })
        },
        idxOfDateCol() {
            // result-data-table auto adds an order number header, so plus 1
            return this.headers.findIndex(h => h.text === 'date') + 1
        },
        persistedQueryData() {
            switch (this.activeMode) {
                case this.QUERY_MODES.HISTORY:
                    return this.query_history
                case this.QUERY_MODES.SNIPPETS:
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
            if (
                this.activeMode === this.QUERY_MODES.HISTORY &&
                this.logTypesToShow.length &&
                this.logTypesToShow.length < this.queryLogTypes.length
            )
                data = data.filter(log => this.logTypesToShow.includes(log.action.type))
            return data.map(item => Object.values(item))
        },
        menuOpts() {
            const { CLIPBOARD, INSERT } = NODE_CTX_TYPES
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
    created() {
        this.OS_KEY = OS_KEY
        this.QUERY_MODES = QUERY_MODES
    },
    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_QUERY_HISTORY: 'prefAndStorage/SET_QUERY_HISTORY',
            SET_QUERY_SNIPPETS: 'prefAndStorage/SET_QUERY_SNIPPETS',
        }),
        setHeaderHeight() {
            if (this.$refs.header) this.headerHeight = this.$refs.header.clientHeight
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
            const newData = this.$helpers.map2dArr({
                fields: this.headers.map(h => h.text),
                arr: newMaxtrices,
            })

            this[`SET_QUERY_${this.activeMode}`](newData)
        },
        txtOptHandler({ opt, data }) {
            let rowData = this.$helpers.map2dArr({
                fields: this.headers.map(h => h.text),
                arr: [data.row.filter((_, i) => i !== 0)], // Remove # col
            })
            let sql, name
            switch (this.activeMode) {
                case this.QUERY_MODES.HISTORY: {
                    name = rowData[0].action.name
                    sql = rowData[0].action.sql
                    break
                }
                case this.QUERY_MODES.SNIPPETS:
                    sql = rowData[0].sql
            }
            const { INSERT, CLIPBOARD } = NODE_CTX_TYPES
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
                value: cell,
                formatType: this.dateFormatType,
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
        height: 24px;
        vertical-align: middle;
        &:first-of-type {
            padding-right: 16px;
        }
    }
}
</style>
