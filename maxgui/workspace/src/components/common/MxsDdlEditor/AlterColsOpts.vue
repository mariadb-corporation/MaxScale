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
                        @on-input-column_type="onInputColumnType"
                        @on-input-PK="onInputPK"
                        @on-input-NN="onInputNN"
                        @on-input-AI="onInputAI"
                        @on-input-generated="onInputGenerated"
                        @on-input-charset="onInputCharset"
                    />
                </div>
            </template>
            <template v-slot:header-column_name="{ data: { maxWidth, activatorID } }">
                <mxs-truncate-str
                    :tooltipItem="{ txt: 'Column Name', activatorID }"
                    :maxWidth="maxWidth"
                />
            </template>
            <template v-slot:header-column_type="{ data: { maxWidth, activatorID } }">
                <mxs-truncate-str
                    :tooltipItem="{ txt: 'Column Type', activatorID }"
                    :maxWidth="maxWidth"
                />
            </template>
            <template
                v-for="(value, key) in abbreviatedHeaders"
                v-slot:[abbrHeaderSlotName(key)]="{ data: { header, maxWidth } }"
            >
                <v-tooltip :key="key" top transition="slide-y-transition" :disabled="isVertTable">
                    <template v-slot:activator="{ on }">
                        <div
                            class="d-inline-block text-truncate"
                            :style="{ maxWidth: `${maxWidth}px` }"
                            v-on="on"
                        >
                            {{ isVertTable ? value : header.text }}
                        </div>
                    </template>
                    <span>{{ value }}</span>
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
        headers() {
            return this.$typy(this.colDefinitions, 'fields').safeArray.map(field => {
                let h = {
                    text: field,
                    sortable: false,
                    capitalize: true,
                }
                switch (field) {
                    case 'PK':
                    case 'NN':
                    case 'UN':
                    case 'UQ':
                    case 'ZF':
                    case 'AI':
                        h.minWidth = 50
                        h.maxWidth = 50
                        h.resizable = false
                        break
                    case 'generated':
                        h.width = 144
                        h.minWidth = 126
                        break
                    case 'id':
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
            return {
                PK: 'PRIMARY KEY',
                NN: 'NOT NULL',
                UN: 'UNSIGNED',
                UQ: 'UNIQUE INDEX',
                ZF: 'ZEROFILL',
                AI: 'AUTO_INCREMENT',
                generated: 'Generated column',
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
        idxOfId() {
            return this.findColOptIdx('id')
        },
        idxOfCollation() {
            return this.findColOptIdx('collation')
        },
        idxOfCharset() {
            return this.findColOptIdx('charset')
        },
        idxOfAI() {
            return this.findColOptIdx('AI')
        },
        idxOfGenerated() {
            return this.findColOptIdx('generated')
        },
        idxOfUN() {
            return this.findColOptIdx('UN')
        },
        idxOfNN() {
            return this.findColOptIdx('NN')
        },
        idxOfUQ() {
            return this.findColOptIdx('UQ')
        },
        idxOfDefAndExp() {
            return this.findColOptIdx('default/expression')
        },
        hasAI() {
            let count = 0
            this.rows.forEach(row => {
                if (row[this.idxOfAI] === 'AUTO_INCREMENT') count++
            })
            return count === 1
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
                const hiddenSpecs = ['charset', 'collation', 'comment']
                this.selectedColSpecs = colSpecs.filter(col => !hiddenSpecs.includes(col.text))
            }
        },
        abbrHeaderSlotName(h) {
            return `header-${h}`
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
            this.headers.forEach(h => {
                switch (h.text) {
                    case 'id':
                        row.push(this.$helpers.uuidv1())
                        break
                    case 'NN':
                        row.push('NULL')
                        break
                    case 'PK':
                        row.push('NO')
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
                columns: this.$typy(this.colDefinitions, 'fields').safeArray,
                rows: [rowData],
            })
            if (rows.length) return rows[0]
            return []
        },
        findColOptIdx(headerName) {
            return this.headers.findIndex(h => h.text === headerName)
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
                const idxOfZF = this.findColOptIdx('ZF')
                return this.$helpers.immutableUpdate(colDefinitions, {
                    data: {
                        [item.alterColIdx]: {
                            [this.idxOfUN]: { $set: '' },
                            [idxOfZF]: { $set: '' },
                            [this.idxOfAI]: { $set: '' },
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
                            [this.idxOfUN]: { $set: 'UNSIGNED' },
                            [this.idxOfNN]: { $set: 'NOT NULL' },
                            [this.idxOfAI]: { $set: 'AUTO_INCREMENT' },
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
         * @param {Object} item - column_type cell data
         */
        onInputColumnType(item) {
            // first update column_type cell
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
                if (row[this.idxOfAI] === 'AUTO_INCREMENT' && i !== alterColIdx) {
                    idx = i
                    break
                }

            if (idx >= 0)
                return (colDefinitions = this.$helpers.immutableUpdate(colDefinitions, {
                    data: {
                        [idx]: {
                            [this.idxOfAI]: { $set: '' },
                        },
                    },
                }))
            return colDefinitions
        },

        /**
         * This updates NN cell and `default` cell.
         * @param {Object} payload.colDefinitions - current colDefinitions
         * @param {Number} payload.alterColIdx - alterColIdx to be updated
         * @param {String} payload.valOfNN - value of NN
         * @param {String} payload.valueOfDefault - value of default cell
         * @returns {Object} - returns new colDefinitions
         */
        notNullSideEffect({ colDefinitions, alterColIdx, valOfNN, valueOfDefault = null }) {
            let defaultVal = this.$typy(
                colDefinitions,
                `data['${alterColIdx}']['${this.idxOfDefAndExp}']`
            ).safeString
            if (defaultVal === 'NULL' && valOfNN === 'NOT NULL') defaultVal = ''
            if (valueOfDefault !== null) defaultVal = valueOfDefault
            return this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [alterColIdx]: {
                        [this.idxOfNN]: { $set: valOfNN },
                        [this.idxOfDefAndExp]: { $set: defaultVal },
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
                        [this.idxOfGenerated]: { $set: '(none)' },
                    },
                },
            })
            this.colDefinitions = this.notNullSideEffect({
                colDefinitions,
                alterColIdx: item.alterColIdx,
                valOfNN: 'NOT NULL',
                // set to empty string when AI value is AUTO_INCREMENT
                valueOfDefault: item.value ? '' : null,
            })
        },
        /**
         * @param {Object} item - G cell data
         */
        onInputGenerated(item) {
            let colDefinitions = this.colDefinitions
            let defaultVal = ''
            if (item.value === '(none)') {
                const nnVal = this.$typy(
                    colDefinitions,
                    `data['${item.alterColIdx}']['${this.idxOfNN}']`
                ).safeString
                if (nnVal === 'NULL') defaultVal = 'NULL'
                else if (nnVal === 'NOT NULL') defaultVal = ''
            }
            // use initial expression value or empty string
            else {
                defaultVal = this.$typy(
                    this.initialData.data,
                    `['${item.alterColIdx}']['${this.idxOfDefAndExp}']`
                ).safeString
            }

            // update G and its dependencies cell value
            colDefinitions = this.$helpers.immutableUpdate(colDefinitions, {
                data: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                        [this.idxOfAI]: { $set: '' },
                        [this.idxOfNN]: { $set: '' },
                        [this.idxOfDefAndExp]: { $set: defaultVal },
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
                valOfNN: 'NOT NULL',
            })
        },
        /**
         * @param {Object} item - NN cell data
         */
        onInputNN(item) {
            this.colDefinitions = this.notNullSideEffect({
                colDefinitions: this.colDefinitions,
                alterColIdx: item.alterColIdx,
                valOfNN: item.value,
            })
        },
    },
}
</script>
