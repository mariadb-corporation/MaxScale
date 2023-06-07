<template>
    <div class="fill-height">
        <div :style="{ height: `${headerHeight}px` }" class="pb-2 d-flex align-center flex-1">
            <v-spacer />
            <mxs-tooltip-btn
                v-if="selectedItems.length"
                btnClass="mr-2 pa-1 text-capitalize"
                x-small
                outlined
                depressed
                color="error"
                @click="deleteSelectedRows(selectedItems)"
            >
                <template v-slot:btn-content>
                    {{ $mxs_t('drop') }} ({{ selectedItems.length }}
                </template>
                {{ $mxs_t('dropSelectedCols') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                btnClass="mr-2 pa-1 text-capitalize"
                x-small
                outlined
                depressed
                color="primary"
                @click="addNewCol"
            >
                <template v-slot:btn-content>
                    {{ $mxs_t('add') }}
                </template>
                {{ $mxs_t('addNewCol') }}
            </mxs-tooltip-btn>
            <mxs-filter-list
                v-model="selectedColSpecs"
                returnObject
                :label="$mxs_t('alterSpecs')"
                :items="colSpecs"
                :maxHeight="tableMaxHeight - 20"
            />
            <mxs-tooltip-btn
                btnClass="ml-2 pa-1"
                x-small
                outlined
                depressed
                color="primary"
                @click="isVertTable = !isVertTable"
            >
                <template v-slot:btn-content>
                    <v-icon size="14" :class="{ 'rotate-left': !isVertTable }">
                        mdi-format-rotate-90
                    </v-icon>
                </template>
                {{ $mxs_t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}
            </mxs-tooltip-btn>
        </div>

        <mxs-virtual-scroll-tbl
            :headers="visHeaders"
            :rows="rows"
            :itemHeight="40"
            :maxHeight="tableMaxHeight"
            :boundingWidth="boundingWidth"
            showSelect
            :isVertTable="isVertTable"
            v-on="$listeners"
            @selected-rows="selectedItems = $event"
        >
            <template
                v-for="(h, colOptIdx) in visHeaders"
                v-slot:[h.text]="{ data: { rowData, cell, rowIdx: alterColIdx } }"
            >
                <div :key="h.text" class="fill-height d-flex align-center">
                    <col-opt-input
                        :ref="`colOptInput-alterColIdx-${alterColIdx}-colOptIdx-${colOptIdx}`"
                        :initialColOptsData="$typy(initialData, `data['${alterColIdx}']`).safeArray"
                        :data="{
                            field: h.text,
                            value: cell,
                            alterColIdx,
                            colOptIdx,
                            rowObj: rowDataToObj(rowData),
                        }"
                        :height="28"
                        :charsetCollationMap="charsetCollationMap"
                        :defTblCharset="defTblCharset"
                        :defTblCollation="defTblCollation"
                        :dataTypes="dataTypes"
                        @on-input="onCellInput"
                        @on-input-type="onInputColumnType"
                        @on-input-PK="onInputPK"
                        @on-input-NN="onInputNN"
                        @on-input-AI="onInputAI"
                        @on-input-generated="onInputGenerated"
                        @on-input-charset="onInputCharset"
                    />
                </div>
            </template>
            <template
                v-for="(value, key) in abbreviatedHeaders"
                v-slot:[`header-${key}`]="{ data: { header, maxWidth } }"
            >
                <v-tooltip :key="key" top transition="slide-y-transition" :disabled="isVertTable">
                    <template v-slot:activator="{ on }">
                        <div
                            class="d-inline-block text-truncate"
                            :style="{ maxWidth: `${maxWidth}px` }"
                            v-on="on"
                        >
                            {{ isVertTable ? header.text : value }}
                        </div>
                    </template>
                    <span>{{ header.text }}</span>
                </v-tooltip>
            </template>
        </mxs-virtual-scroll-tbl>
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import ColOptInput from '@wsSrc/components/common/MxsDdlEditor/ColOptInput.vue'
import {
    getColumnTypes,
    check_charset_support,
    check_UN_ZF_support,
    check_AI_support,
} from '@wsSrc/components/common/MxsDdlEditor/utils.js'

export default {
    name: 'alter-cols-opts',
    components: { ColOptInput },
    props: {
        value: { type: Object, required: true },
        initialData: { type: Object, required: true },
        height: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        defTblCharset: { type: String, required: true },
        defTblCollation: { type: String, required: true },
        charsetCollationMap: { type: Object, required: true },
    },
    data() {
        return {
            selectedItems: [],
            isVertTable: false,
            selectedColSpecs: [],
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            GENERATED_TYPES: state => state.mxsWorkspace.config.GENERATED_TYPES,
        }),
        headerHeight() {
            return 28
        },
        tableMaxHeight() {
            return this.height - this.headerHeight
        },
        colDefinitions: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        colAttrs() {
            return Object.values(this.COL_ATTRS)
        },
        headers() {
            const { ID, PK, NN, UN, UQ, ZF, AI, GENERATED_TYPE } = this.COL_ATTRS
            return this.colAttrs.map(field => {
                let h = {
                    text: field,
                    sortable: false,
                    capitalize: true,
                }
                switch (field) {
                    case PK:
                    case NN:
                    case UN:
                    case UQ:
                    case ZF:
                    case AI:
                        h.minWidth = 50
                        h.maxWidth = 50
                        h.resizable = false
                        break
                    case GENERATED_TYPE:
                        h.width = 144
                        h.minWidth = 126
                        break
                    case ID:
                        h.hidden = true
                        break
                }
                return h
            })
        },
        colSpecs() {
            return this.headers.filter(h => !h.hidden)
        },
        visHeaders() {
            return this.headers.map(h => {
                if (!this.selectedColSpecs.find(col => col.text === h.text))
                    return { ...h, hidden: true }
                return h
            })
        },
        abbreviatedHeaders() {
            const { PK, NN, UN, UQ, ZF, AI, GENERATED_TYPE, CHARSET } = this.COL_ATTRS
            return {
                [PK]: 'PK',
                [NN]: 'NN',
                [UN]: 'UN',
                [UQ]: 'UQ',
                [ZF]: 'ZF',
                [AI]: 'AI',
                [GENERATED_TYPE]: 'GENERATED',
                [CHARSET]: 'CHARSET',
            }
        },
        rows() {
            return this.$typy(this.colDefinitions, 'data').safeArray
        },
        dataTypes() {
            let items = []
            getColumnTypes().forEach(item => {
                // place header first, then all its types and add a divider
                items = [...items, { header: item.header }, ...item.types, { divider: true }]
            })
            return items
        },
        idxOfCollation() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.COLLATE]
        },
        idxOfCharset() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.CHARSET]
        },
        idxOfAI() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.AI]
        },
        idxOfGenType() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.GENERATED_TYPE]
        },
        idxOfUN() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.UN]
        },
        idxOfNN() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NN]
        },
        idxOfUQ() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.UQ]
        },
        idxOfDefAndExp() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.DEF_EXP]
        },
        hasAI() {
            return this.rows.some(row => row[this.idxOfAI])
        },
    },
    mounted() {
        this.handleShowColSpecs()
    },
    methods: {
        handleShowColSpecs() {
            const colSpecs = this.$helpers.lodash.cloneDeep(this.colSpecs)
            if (this.$vuetify.breakpoint.width >= 1680) this.selectedColSpecs = colSpecs
            else {
                const { CHARSET, COLLATE, COMMENT } = this.COL_ATTRS
                const hiddenSpecs = [CHARSET, COLLATE, COMMENT]
                this.selectedColSpecs = colSpecs.filter(col => !hiddenSpecs.includes(col.text))
            }
        },
        deleteSelectedRows(selectedItems) {
            const { xorWith, isEqual } = this.$helpers.lodash
            this.colDefinitions = {
                ...this.colDefinitions,
                data: xorWith(this.colDefinitions.data, selectedItems, isEqual),
            }
        },
        addNewCol() {
            let row = []
            const { ID, PK, NN, UN, ZF, AI, GENERATED_TYPE } = this.COL_ATTRS
            this.headers.forEach(h => {
                switch (h.text) {
                    case ID:
                        row.push(this.$helpers.uuidv1())
                        break
                    case PK:
                    case NN:
                    case UN:
                    case ZF:
                    case AI:
                        row.push(false)
                        break
                    case GENERATED_TYPE:
                        row.push(this.GENERATED_TYPES.NONE)
                        break
                    default:
                        row.push('')
                        break
                }
            })
            this.colDefinitions.data.push(row)
        },
        rowDataToObj(rowData) {
            const rows = this.$helpers.getObjectRows({
                columns: this.colAttrs,
                rows: [rowData],
            })
            if (rows.length) return rows[0]
            return []
        },
        /**
         * @param {Object} item - cell data
         */
        onCellInput(item) {
            this.colDefinitions = this.$helpers.immutableUpdate(this.colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                    },
                },
            })
        },
        /**
         * This patches charset and collation at provided alterColIdx
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Number} payload.alterColIdx - row to be updated
         * @param {String} payload.charset - charset to set.
         * @returns {Object} - returns new colDefinitions
         */
        patchCharsetCollation({ colDefinitions, alterColIdx, charset }) {
            return this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [alterColIdx]: {
                        [this.idxOfCharset]: { $set: charset },
                        [this.idxOfCollation]: {
                            $set: this.$typy(this.charsetCollationMap, `[${charset}].defCollation`)
                                .safeString,
                        },
                    },
                },
            })
        },
        /**
         * This handles auto set charset/collation to use utf8
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colDefinitions
         */
        handleNationalType({ colDefinitions, item }) {
            if (item.value.includes('NATIONAL'))
                return this.patchCharsetCollation({
                    colDefinitions,
                    alterColIdx: item.alterColIdx,
                    charset: 'utf8',
                })
            return colDefinitions
        },
        /**
         * This handles auto uncheck UN, ZF, AI checkboxes if chosen type doesn't support
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colDefinitions
         */
        handleUncheck_UN_ZF_AI({ colDefinitions, item }) {
            if (!check_UN_ZF_support(item.value) || !check_AI_support(item.value)) {
                const idxOfZF = this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ZF]
                return this.$helpers.immutableUpdate(colDefinitions, {
                    data: {
                        [item.alterColIdx]: {
                            [this.idxOfUN]: { $set: false },
                            [idxOfZF]: { $set: false },
                            [this.idxOfAI]: { $set: false },
                        },
                    },
                })
            }
            return colDefinitions
        },
        /**
         * This handles set default charset/collation
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colDefinitions
         */
        handleSetDefCharset({ colDefinitions, item }) {
            let charset = null,
                collation = null
            if (check_charset_support(item.value)) {
                charset = this.defTblCharset
                collation = this.defTblCollation
            }
            return this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [this.idxOfCharset]: { $set: charset },
                        [this.idxOfCollation]: { $set: collation },
                    },
                },
            })
        },
        /**
         * This handles SERIAL type
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new colDefinitions
         */
        handleSerialType({ colDefinitions, item }) {
            if (item.value === 'SERIAL') {
                const colOptInput = this.$refs[
                    `colOptInput-alterColIdx-${item.alterColIdx}-colOptIdx-${this.idxOfUQ}`
                ][0]
                return this.$helpers.immutableUpdate(colDefinitions, {
                    data: {
                        [item.alterColIdx]: {
                            [this.idxOfUN]: { $set: true },
                            [this.idxOfNN]: { $set: true },
                            [this.idxOfAI]: { $set: true },
                            [this.idxOfUQ]: {
                                $set: colOptInput.uniqueIdxName,
                            },
                        },
                    },
                })
            }
            return colDefinitions
        },
        /**
         * @param {Object} item - type cell data
         */
        onInputColumnType(item) {
            // first update type cell
            let colDefinitions = this.$helpers.immutableUpdate(this.colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                    },
                },
            })
            colDefinitions = this.handleSetDefCharset({ colDefinitions, item })
            colDefinitions = this.handleUncheck_UN_ZF_AI({ colDefinitions, item })
            colDefinitions = this.handleNationalType({ colDefinitions, item })
            colDefinitions = this.handleSerialType({ colDefinitions, item })
            this.colDefinitions = colDefinitions
        },
        /**
         * This unchecks the other auto_increment as there
         * can be one table column has this.
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Number} payload.alterColIdx - alterColIdx to be excluded
         * @returns {Object} - returns new colDefinitions
         */
        uncheckOtherAI({ colDefinitions, alterColIdx }) {
            let idx
            for (const [i, row] of this.rows.entries())
                if (row[this.idxOfAI] && i !== alterColIdx) {
                    idx = i
                    break
                }
            if (idx >= 0)
                colDefinitions = this.$helpers.immutableUpdate(colDefinitions, {
                    data: {
                        [idx]: { [this.idxOfAI]: { $set: false } },
                    },
                })
            return colDefinitions
        },
        /**
         * @param {number} - index of the column
         * @returns {string} - initial value of DEF_EXP attr
         */
        getInitialDefaultExp(colIdx) {
            return this.$typy(this.initialData, `data['${colIdx}']['${this.idxOfDefAndExp}']`)
                .safeString
        },
        /**
         * This updates NN cell and `default` cell.
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Number} payload.alterColIdx - alterColIdx to be updated
         * @param {boolean} payload.isNN - is NOT NULL
         * @param {string} payload.valueOfDefault - value of default cell
         * @returns {Object} - returns new colDefinitions
         */
        notNullSideEffect({ colDefinitions, alterColIdx, isNN }) {
            return this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [alterColIdx]: {
                        [this.idxOfNN]: { $set: isNN },
                        [this.idxOfDefAndExp]: {
                            $set: isNN ? '' : this.getInitialDefaultExp(alterColIdx),
                        },
                    },
                },
            })
        },
        /**
         * @param {Object} item - AI cell data
         */
        onInputAI(item) {
            let colDefinitions = this.colDefinitions
            if (this.hasAI)
                colDefinitions = this.uncheckOtherAI({
                    colDefinitions,
                    alterColIdx: item.alterColIdx,
                })
            // update AI and generated cells
            colDefinitions = this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                        [this.idxOfGenType]: { $set: this.GENERATED_TYPES.NONE },
                    },
                },
            })
            this.colDefinitions = this.notNullSideEffect({
                colDefinitions,
                alterColIdx: item.alterColIdx,
                isNN: true,
            })
        },
        /**
         * @param {Object} item - G cell data
         */
        onInputGenerated(item) {
            let colDefinitions = this.colDefinitions
            // update G and its dependencies cell value
            colDefinitions = this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                        [this.idxOfAI]: { $set: false },
                        [this.idxOfNN]: { $set: false },
                    },
                },
            })
            this.colDefinitions = colDefinitions
        },
        /**
         * @param {Object} item - charset cell data
         */
        onInputCharset(item) {
            this.colDefinitions = this.patchCharsetCollation({
                colDefinitions: this.colDefinitions,
                alterColIdx: item.alterColIdx,
                charset: item.value,
            })
        },
        /**
         * @param {Object} item - PK cell data
         */
        onInputPK(item) {
            // update PK and UQ value
            let colDefinitions = this.$helpers.immutableUpdate(this.colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                        [this.idxOfUQ]: { $set: '' },
                    },
                },
            })
            this.colDefinitions = this.notNullSideEffect({
                colDefinitions,
                alterColIdx: item.alterColIdx,
                isNN: true,
            })
        },
        /**
         * @param {Object} item - NN cell data
         */
        onInputNN(item) {
            this.colDefinitions = this.notNullSideEffect({
                colDefinitions: this.colDefinitions,
                alterColIdx: item.alterColIdx,
                isNN: item.value,
            })
        },
    },
}
</script>
