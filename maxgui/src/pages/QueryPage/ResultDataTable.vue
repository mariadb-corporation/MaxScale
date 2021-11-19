<template>
    <div>
        <div ref="tableTools" class="table-tools pb-2 d-inline-flex align-center">
            <slot name="left-table-tools-prepend" />
            <v-text-field
                v-model="filterKeyword"
                name="filter"
                dense
                outlined
                height="28"
                class="std filter-result mr-2"
                :placeholder="$t('filterResult')"
                hide-details
            />
            <column-list
                v-model="filterHeaderIdxs"
                selectAllOnActivated
                :label="$t('filterBy')"
                :cols="tableHeaders"
                :maxHeight="tableHeight - 20"
            />
            <slot name="left-table-tools-append" />
            <v-spacer />
            <slot name="right-table-tools-prepend" />
            <v-tooltip
                v-if="selectedItems.length"
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="mr-2 pa-1 text-capitalize"
                        outlined
                        depressed
                        color="error"
                        v-on="on"
                        @click="$emit('on-delete-selected', selectedItems)"
                    >
                        {{ $t('delete') }} ({{ selectedItems.length }})
                    </v-btn>
                </template>
                <span>{{ $t('deleteSelectedRows') }}</span>
            </v-tooltip>
            <result-export :rows="filteredRows_wo_idx" :headers="visHeaders_wo_idx" />
            <column-list
                v-model="visHeaderIdxs"
                selectAllOnActivated
                :label="$t('columns')"
                :cols="tableHeaders"
                :maxHeight="tableHeight - 20"
            />
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="ml-2 pa-1"
                        outlined
                        depressed
                        color="accent-dark"
                        :disabled="isGrouping"
                        v-on="on"
                        @click="isVertTable = !isVertTable"
                    >
                        <v-icon
                            size="14"
                            color="accent-dark"
                            :class="{ 'rotate-icon__vert': !isVertTable }"
                        >
                            rotate_90_degrees_ccw
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}</span>
            </v-tooltip>
            <slot name="right-table-tools-append" />
        </div>
        <!-- Keep it in memory, negative height crashes v-virtual-scroll -->
        <keep-alive>
            <virtual-scroll-table
                v-if="tableHeight > 0"
                class="pb-2"
                :headers="visibleHeaders"
                :rows="filteredRows"
                :itemHeight="30"
                :maxHeight="tableHeight"
                :boundingWidth="width"
                :isVertTable="isVertTable"
                :showSelect="showSelect"
                :groupBy="groupBy"
                :activeRow="activeRow"
                @item-selected="selectedItems = $event"
                @is-grouping="isGrouping = $event"
                @on-cell-right-click="onCellRClick"
                v-on="$listeners"
            >
                <template
                    v-for="h in visibleHeaders"
                    v-slot:[h.text]="{ data: { cell, header, maxWidth } }"
                >
                    <slot :name="`${h.text}`" :data="{ cell, header, maxWidth }" />
                </template>
                <template v-for="h in visibleHeaders" v-slot:[`header-${h.text}`]="{ data }">
                    <slot :name="`header-${h.text}`" :data="data" />
                </template>
            </virtual-scroll-table>
        </keep-alive>
        <sub-menu
            v-if="!$typy(ctxMenuData).isEmptyObject"
            :key="ctxMenuActivator"
            v-model="showCtxMenu"
            left
            :items="menuItems"
            :activator="ctxMenuActivator"
            @item-click="onChooseOpt"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
@on-delete-selected: selectedItems:any[]. Event is emitted when showSelect props is true
Also emits other events from virtual-scroll-table via v-on="$listeners"
*/
import ResultExport from './ResultExport'
import ColumnList from './ColumnList.vue'
import { mapState } from 'vuex'
export default {
    name: 'result-data-table',
    components: {
        'result-export': ResultExport,
        'column-list': ColumnList,
    },
    props: {
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        rows: { type: Array, required: true },
        height: { type: Number, required: true },
        width: { type: Number, required: true },
        showSelect: { type: Boolean, default: false },
        groupBy: { type: String, default: '' },
        showGroupBy: { type: Boolean, default: false },
        //menuOpts:[{ text:string, type:string, action:function}]
        menuOpts: { type: Array, default: () => [] },
    },
    data() {
        return {
            filterHeaderIdxs: [],
            visHeaderIdxs: [],
            filterKeyword: '',
            tableToolsHeight: 0,
            isVertTable: false,
            isGrouping: false,
            selectedItems: [],
            // states for ctx menu
            showCtxMenu: false,
            ctxMenuData: {},
        }
    },
    computed: {
        ...mapState({
            SQL_RES_TBL_CTX_OPT_TYPES: state => state.app_config.SQL_RES_TBL_CTX_OPT_TYPES,
        }),
        tableHeight() {
            return this.height - this.tableToolsHeight - 8
        },
        tableHeaders() {
            let headers = []
            if (this.headers.length)
                headers = [
                    { text: '#', maxWidth: 'max-content' },
                    ...this.headers.map(h =>
                        this.showGroupBy && !this.$typy(h, 'groupable').isDefined
                            ? { ...h, groupable: true, draggable: true }
                            : { ...h, draggable: true }
                    ),
                ]

            return headers
        },
        rowsWithIndex() {
            return this.rows.map((row, i) => [i + 1, ...row])
        },
        filteredRows_wo_idx() {
            return this.filteredRows.map(row => row.filter((cell, i) => i !== 0))
        },
        visHeaders_wo_idx() {
            return this.visibleHeaders.filter(header => header.text !== '#')
        },
        filteredRows() {
            return this.rowsWithIndex.filter(row => {
                let match = false
                for (const [i, cell] of row.entries()) {
                    if (
                        (this.filterHeaderIdxs.includes(i) || !this.filterHeaderIdxs.length) &&
                        this.$help.ciStrIncludes(`${cell}`, this.filterKeyword)
                    ) {
                        match = true
                        break
                    }
                }
                return match
            })
        },
        visibleHeaders() {
            return this.tableHeaders.map((h, i) =>
                this.visHeaderIdxs.includes(i) ? h : { ...h, hidden: true }
            )
        },
        activeRow() {
            return this.$typy(this.ctxMenuData, 'row').safeArray
        },
        ctxMenuActivator() {
            return `#${this.$typy(this.ctxMenuData, 'cellID').safeString}`
        },
        clipboardOpts() {
            const { CLIPBOARD } = this.SQL_RES_TBL_CTX_OPT_TYPES
            return this.genTxtOpts(CLIPBOARD)
        },
        insertOpts() {
            const {
                TXT_EDITOR: { INSERT },
            } = this.SQL_RES_TBL_CTX_OPT_TYPES
            return this.genTxtOpts(INSERT)
        },
        baseOpts() {
            return [
                {
                    text: this.$t('placeToEditor'),
                    children: this.insertOpts,
                },
                {
                    text: this.$t('copyToClipboard'),
                    children: this.clipboardOpts,
                },
            ]
        },
        menuItems() {
            if (this.menuOpts.length) {
                // Deep merge of menuOpts with baseOpts
                const { deepMergeWith, keyBy, values } = this.$help.lodash
                const merged = values(
                    deepMergeWith(
                        keyBy(this.baseOpts, 'text'),
                        keyBy(this.menuOpts, 'text'),
                        (objVal, srcVal) => {
                            if (Array.isArray(objVal)) {
                                return objVal.concat(srcVal)
                            }
                        }
                    )
                )
                return merged
            }
            return this.baseOpts
        },
    },
    watch: {
        showCtxMenu(v) {
            // when menu is closed by blur event, clear ctxMenuData so that activeRow can be reset
            if (!v) this.ctxMenuData = {}
        },
    },
    activated() {
        this.setTableToolsHeight()
    },
    methods: {
        setTableToolsHeight() {
            if (!this.$refs.tableTools) return
            this.tableToolsHeight = this.$refs.tableTools.clientHeight
        },
        /**
         * @param {Object} data { e: event, row:[], cell:string, cellID:string }
         */
        onCellRClick(data) {
            const { cellID } = data
            if (this.$typy(this.ctxMenuData, 'cellID').safeString === cellID) {
                this.showCtxMenu = false
                this.ctxMenuData = {}
            } else {
                this.showCtxMenu = true
                this.ctxMenuData = data
            }
        },
        /**
         * Both INSERT and CLIPBOARD types have same options & action
         * This generates txt options based on provided type
         * @param {String} type - INSERT OR CLIPBOARD
         * @returns {Array} - return context options
         */
        genTxtOpts(type) {
            return [this.$t('fieldQuoted'), this.$t('field')].map(text => ({
                text,
                action: ({ opt, data }) => this.handleTxtOpt({ opt, data }),
                type,
            }))
        },
        // Handle edge case when cell value is an object. e.g. In History table
        processField(cell) {
            // convert to string with template literals
            return this.$typy(cell).isObject ? `${cell.name}` : `${cell}`
        },
        /**
         * Both INSERT and CLIPBOARD types have same options and action
         * This handles INSERT and CLIPBOARD options
         * @param {data} item - data
         * @param {Object} opt - context menu option
         */
        handleTxtOpt({ opt, data }) {
            const {
                CLIPBOARD,
                TXT_EDITOR: { INSERT },
            } = this.SQL_RES_TBL_CTX_OPT_TYPES
            let v = ''
            switch (opt.text) {
                case this.$t('fieldQuoted'):
                    v = this.$help.escapeIdentifiers(this.processField(data.cell))
                    break
                case this.$t('field'):
                    v = this.processField(data.cell)
                    break
            }
            switch (opt.type) {
                case INSERT:
                    this.$emit('place-to-editor', v)
                    break
                case CLIPBOARD:
                    this.$help.copyTextToClipboard(v)
                    break
            }
        },
        onChooseOpt(opt) {
            // pass arguments opt and data to action function
            opt.action({ opt, data: this.ctxMenuData })
        },
    },
}
</script>

<style lang="scss" scoped>
.std.filter-result {
    max-width: 250px;
}
.table-tools {
    width: 100%;
}
.rotate-icon__vert {
    transform: rotate(90deg);
}
</style>
