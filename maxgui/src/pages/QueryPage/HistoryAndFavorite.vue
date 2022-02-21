<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <v-tabs v-model="activeView" hide-slider :height="20" class="tab-navigation--btn-style">
                <v-tab
                    :key="SQL_QUERY_MODES.HISTORY"
                    :href="`#${SQL_QUERY_MODES.HISTORY}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $t('history') }}
                </v-tab>
                <v-tab
                    :key="SQL_QUERY_MODES.FAVORITE"
                    :href="`#${SQL_QUERY_MODES.FAVORITE}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $t('favorite') }}
                </v-tab>
            </v-tabs>
        </div>
        <keep-alive>
            <template v-if="persistedQueryData.length">
                <table-list
                    v-if="
                        activeView === SQL_QUERY_MODES.HISTORY ||
                            activeView === SQL_QUERY_MODES.FAVORITE
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
                    @on-delete-selected="handleDeleteSelectedRows"
                    @custom-group="customGroup"
                    v-on="$listeners"
                >
                    <template v-slot:header-connection_name="{ data: { maxWidth } }">
                        <truncate-string
                            :key="maxWidth"
                            class="text-truncate"
                            text="Connection Name"
                            :maxWidth="maxWidth"
                        />
                    </template>
                    <template v-slot:date="{ data: { cell, maxWidth } }">
                        <truncate-string
                            :text="
                                `${$help.dateFormat({
                                    value: cell,
                                    formatType: 'ddd, DD MMM YYYY',
                                })}`
                            "
                            :maxWidth="maxWidth"
                        />
                    </template>
                    <template v-slot:action="{ data: { cell, maxWidth } }">
                        <v-tooltip
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop color text-navigation pa-2 pb-4"
                        >
                            <template v-slot:activator="{ on }">
                                <span
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
                                        $t('queryResInfo')
                                    }}
                                    <v-divider class="color border-separator" />
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
                            <filter-list
                                v-model="selectedLogTypes"
                                selectAllOnActivated
                                :label="$t('logTypes')"
                                :items="queryLogTypes"
                                returnObject
                                :maxHeight="200"
                            />
                        </div>
                    </template>
                </table-list>
            </template>
            <span
                v-else
                v-html="
                    activeView === SQL_QUERY_MODES.HISTORY
                        ? $t('historyTabGuide')
                        : $t('favoriteTabGuide')
                "
            />
        </keep-alive>
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="
                $t('clearSelectedQueries', {
                    targetType: $t(
                        activeView === SQL_QUERY_MODES.HISTORY ? 'queryHistory' : 'favoriteQueries'
                    ),
                })
            "
            type="delete"
            minBodyWidth="624px"
            :onSave="deleteSelectedRows"
        >
            <template v-slot:confirm-text>
                <p>
                    {{
                        $t('info.clearSelectedQueries', {
                            quantity:
                                itemsToBeDeleted.length === rows.length
                                    ? $t('entire')
                                    : $t('selected'),
                            targetType: $t(
                                activeView === SQL_QUERY_MODES.HISTORY
                                    ? 'queryHistory'
                                    : 'favoriteQueries'
                            ),
                        })
                    }}
                </p>
            </template>
        </confirm-dialog>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'history-and-favorite',
    components: {
        'table-list': ResultDataTable,
    },
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
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            QUERY_LOG_TYPES: state => state.app_config.QUERY_LOG_TYPES,
            SQL_RES_TBL_CTX_OPT_TYPES: state => state.app_config.SQL_RES_TBL_CTX_OPT_TYPES,
            curr_query_mode: state => state.query.curr_query_mode,
            query_history: state => state.persisted.query_history,
            query_favorite: state => state.persisted.query_favorite,
        }),
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (
                    this.curr_query_mode === this.SQL_QUERY_MODES.HISTORY ||
                    this.curr_query_mode === this.SQL_QUERY_MODES.FAVORITE
                )
                    this.SET_CURR_QUERY_MODE(value)
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
                case this.SQL_QUERY_MODES.FAVORITE:
                    data = this.query_favorite
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
                        header.hasCustomGroup = true
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
                        header.draggable = true
                        break
                    case 'name':
                        header.width = 240
                        break
                    case 'sql':
                        header.draggable = true
                }
                return header
            })
        },
        persistedQueryData() {
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY:
                    return this.query_history
                case this.SQL_QUERY_MODES.FAVORITE:
                    return this.query_favorite
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
                    text: this.$t('copyToClipboard'),
                    children: [
                        {
                            text: 'SQL',
                            type: CLIPBOARD,
                            action: ({ opt, data }) => this.txtOptHandler({ opt, data }),
                        },
                    ],
                },
                {
                    text: this.$t('placeToEditor'),
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
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
            SET_QUERY_HISTORY: 'persisted/SET_QUERY_HISTORY',
            SET_QUERY_FAVORITE: 'persisted/SET_QUERY_FAVORITE',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        /** Custom groups 2d array with same value at provided index to a Map
         * @param {Array} data.rows - 2d array to be grouped into a Map
         * @param {Number} data.idx - col index of the inner array
         * @param {Object} data.header - header object
         * @param {Function} callback - Callback function to pass the result
         */
        customGroup(data, callback) {
            const { rows, idx, header } = data
            switch (header.text) {
                case 'date': {
                    let map = new Map()
                    rows.forEach(row => {
                        const key = this.$help.dateFormat({
                            value: row[idx],
                            formatType: 'ddd, DD MMM YYYY',
                        })
                        let matrix = map.get(key) || [] // assign an empty arr if not found
                        matrix.push(row)
                        map.set(key, matrix)
                    })
                    callback(map)
                }
            }
        },
        handleDeleteSelectedRows(itemsToBeDeleted) {
            this.itemsToBeDeleted = itemsToBeDeleted
            this.isConfDlgOpened = true
        },
        deleteSelectedRows() {
            const { cloneDeep, xorWith, isEqual } = this.$help.lodash
            let targetMatrices = cloneDeep(this.itemsToBeDeleted).map(
                row => row.filter((_, i) => i !== 0) // Remove # col
            )
            const newMaxtrices = xorWith(this.rows, targetMatrices, isEqual)
            // Convert to array of objects
            const newData = this.$help.getObjectRows({
                columns: this.headers.map(h => h.text),
                rows: newMaxtrices,
            })

            this[`SET_QUERY_${this.activeView}`](newData)
        },
        txtOptHandler({ opt, data }) {
            let rowData = this.$help.getObjectRows({
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
                case this.SQL_QUERY_MODES.FAVORITE:
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
                    this.$help.copyTextToClipboard(sqlTxt)
                    break
                case INSERT:
                    this.$emit('place-to-editor', sqlTxt)
                    break
            }
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
