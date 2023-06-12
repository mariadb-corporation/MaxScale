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
                :label="$mxs_t('specs')"
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
            :rows="cols"
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
                        @on-input="onChangeInput"
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
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'col-definitions',
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
        definitions: {
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
        // column definitions
        cols() {
            return this.$typy(this.definitions, 'cols').safeArray
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
            return this.cols.some(row => row[this.idxOfAI])
        },
        initialKeys() {
            return this.$typy(this.initialData, 'keys').safeObjectOrEmpty
        },
        initialPkColNames() {
            return queryHelper.getColNamesByAttr({
                cols: this.$typy(this.initialData, 'cols').safeArray,
                attr: this.COL_ATTRS.PK,
            })
        },
        initialPkKeys() {
            return this.$typy(this.initialKeys, `[${this.CREATE_TBL_TOKENS.primaryKey}]`).safeArray
        },
        initialUqColNames() {
            return queryHelper.getColNamesByAttr({
                cols: this.$typy(this.initialData, 'cols').safeArray,
                attr: this.COL_ATTRS.UQ,
            })
        },
        initialUqKeys() {
            return this.$typy(this.initialKeys, `[${this.CREATE_TBL_TOKENS.uniqueKey}]`).safeArray
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
            this.definitions = {
                ...this.definitions,
                cols: xorWith(this.definitions.cols, selectedItems, isEqual),
            }
        },
        addNewCol() {
            let row = []
            const { ID, PK, NN, UN, UQ, ZF, AI, GENERATED_TYPE } = this.COL_ATTRS
            this.headers.forEach(h => {
                switch (h.text) {
                    case ID:
                        row.push(this.$helpers.uuidv1())
                        break
                    case PK:
                    case NN:
                    case UN:
                    case UQ:
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
            this.definitions.cols.push(row)
        },
        rowDataToObj(rowData) {
            const cols = this.$helpers.map2dArr({
                fields: this.colAttrs,
                arr: [rowData],
            })
            if (cols.length) return cols[0]
            return []
        },
        /**
         * @param {Object} item - cell data
         */
        onChangeInput(item) {
            let definitions = this.$helpers.immutableUpdate(this.definitions, {
                cols: {
                    [item.alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                    },
                },
            })
            const { TYPE, PK, NN, UQ, AI, GENERATED_TYPE, CHARSET } = this.COL_ATTRS
            switch (item.field) {
                case TYPE:
                    definitions = this.onChangeType({ definitions, item })
                    break
                case PK:
                    definitions = this.onTogglePk({ definitions, item })
                    definitions = this.keySideEffect({ definitions, category: PK })
                    break
                case NN:
                    definitions = this.notNullSideEffect({
                        definitions,
                        alterColIdx: item.alterColIdx,
                        isNN: item.value,
                    })
                    break
                case UQ:
                    definitions = this.keySideEffect({ definitions, category: UQ })
                    break
                case AI:
                    definitions = this.onToggleAi({ definitions, item })
                    break
                case GENERATED_TYPE:
                    definitions = this.$helpers.immutableUpdate(definitions, {
                        cols: {
                            [item.alterColIdx]: {
                                [this.idxOfAI]: { $set: false },
                                [this.idxOfNN]: { $set: false },
                            },
                        },
                    })
                    break
                case CHARSET:
                    definitions = this.patchCharsetCollation({
                        definitions,
                        alterColIdx: item.alterColIdx,
                        charset: item.value,
                    })
                    break
            }
            this.definitions = definitions
        },
        /**
         * This patches charset and collation at provided alterColIdx
         * @param {Object} payload.definitions - current definitions
         * @param {Number} payload.alterColIdx - row to be updated
         * @param {String} payload.charset - charset to set.
         * @returns {Object} - returns new definitions
         */
        patchCharsetCollation({ definitions, alterColIdx, charset }) {
            return this.$helpers.immutableUpdate(definitions, {
                cols: {
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
         * @param {Object} payload.definitions - current definitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new definitions
         */
        handleNationalType({ definitions, item }) {
            if (item.value.includes('NATIONAL'))
                return this.patchCharsetCollation({
                    definitions,
                    alterColIdx: item.alterColIdx,
                    charset: 'utf8',
                })
            return definitions
        },
        /**
         * This handles auto uncheck UN, ZF, AI checkboxes if chosen type doesn't support
         * @param {Object} payload.definitions - current definitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new definitions
         */
        handleUncheck_UN_ZF_AI({ definitions, item }) {
            if (!check_UN_ZF_support(item.value) || !check_AI_support(item.value)) {
                const idxOfZF = this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ZF]
                return this.$helpers.immutableUpdate(definitions, {
                    cols: {
                        [item.alterColIdx]: {
                            [this.idxOfUN]: { $set: false },
                            [idxOfZF]: { $set: false },
                            [this.idxOfAI]: { $set: false },
                        },
                    },
                })
            }
            return definitions
        },
        /**
         * This handles set default charset/collation
         * @param {Object} payload.definitions - current definitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new definitions
         */
        handleSetDefCharset({ definitions, item }) {
            let charset = null,
                collation = null
            if (check_charset_support(item.value)) {
                charset = this.defTblCharset
                collation = this.defTblCollation
            }
            return this.$helpers.immutableUpdate(definitions, {
                cols: {
                    [item.alterColIdx]: {
                        [this.idxOfCharset]: { $set: charset },
                        [this.idxOfCollation]: { $set: collation },
                    },
                },
            })
        },
        /**
         * This handles SERIAL type
         * @param {Object} payload.definitions - current definitions
         * @param {Object} payload.item - cell item
         * @returns {Object} - returns new definitions
         */
        handleSerialType({ definitions, item }) {
            let defs = definitions
            if (item.value === 'SERIAL') {
                defs = this.uncheckOtherAI({ definitions: defs, alterColIdx: item.alterColIdx })
                defs = this.$helpers.immutableUpdate(defs, {
                    cols: {
                        [item.alterColIdx]: {
                            [this.idxOfUN]: { $set: true },
                            [this.idxOfNN]: { $set: true },
                            [this.idxOfAI]: { $set: true },
                            [this.idxOfUQ]: { $set: true },
                        },
                    },
                })
                defs = this.keySideEffect({ definitions: defs, category: this.COL_ATTRS.UQ })
                return defs
            }
            return defs
        },
        onChangeType({ definitions, item }) {
            let defs = definitions
            defs = this.handleSetDefCharset({ definitions, item })
            defs = this.handleUncheck_UN_ZF_AI({ definitions, item })
            defs = this.handleNationalType({ definitions, item })
            defs = this.handleSerialType({ definitions, item })
            return defs
        },
        /**
         * This unchecks the other auto_increment as there
         * can be one table column has this.
         * @param {Object} payload.definitions - current definitions
         * @param {Number} payload.alterColIdx - alterColIdx to be excluded
         * @returns {Object} - returns new definitions
         */
        uncheckOtherAI({ definitions, alterColIdx }) {
            let idx
            for (const [i, col] of this.cols.entries())
                if (col[this.idxOfAI] && i !== alterColIdx) {
                    idx = i
                    break
                }
            if (idx >= 0)
                definitions = this.$helpers.immutableUpdate(definitions, {
                    cols: {
                        [idx]: { [this.idxOfAI]: { $set: false } },
                    },
                })
            return definitions
        },
        /**
         * @param {number} - index of the column
         * @returns {string} - initial value of DEF_EXP attr
         */
        getInitialDefaultExp(colIdx) {
            return this.$typy(this.initialData, `cols['${colIdx}']['${this.idxOfDefAndExp}']`)
                .safeString
        },
        /**
         * This updates NN cell and `default` cell.
         * @param {Object} payload.definitions - current definitions
         * @param {Number} payload.alterColIdx - alterColIdx to be updated
         * @param {boolean} payload.isNN - is NOT NULL
         * @param {string} payload.valueOfDefault - value of default cell
         * @returns {Object} - returns new definitions
         */
        notNullSideEffect({ definitions, alterColIdx, isNN }) {
            return this.$helpers.immutableUpdate(definitions, {
                cols: {
                    [alterColIdx]: {
                        [this.idxOfNN]: { $set: isNN },
                        [this.idxOfDefAndExp]: {
                            $set: isNN ? '' : this.getInitialDefaultExp(alterColIdx),
                        },
                    },
                },
            })
        },
        onToggleAi({ definitions, item }) {
            let defs = definitions
            if (this.hasAI)
                defs = this.uncheckOtherAI({ definitions: defs, alterColIdx: item.alterColIdx })
            // update generated cells
            defs = this.$helpers.immutableUpdate(defs, {
                cols: {
                    [item.alterColIdx]: {
                        [this.idxOfGenType]: { $set: this.GENERATED_TYPES.NONE },
                    },
                },
            })

            return this.notNullSideEffect({
                definitions: defs,
                alterColIdx: item.alterColIdx,
                isNN: true,
            })
        },
        onTogglePk({ definitions, item }) {
            let defs = definitions
            // update UQ key value
            defs = this.$helpers.immutableUpdate(defs, {
                cols: { [item.alterColIdx]: { [this.idxOfUQ]: { $set: false } } },
            })
            defs = this.keySideEffect({ definitions: defs, category: this.COL_ATTRS.UQ })
            defs = this.notNullSideEffect({
                definitions: defs,
                alterColIdx: item.alterColIdx,
                isNN: true,
            })
            return defs
        },
        /**
         * Update either PK or UQ in `keys` field.
         * @param {object} param.definitions - column definitions
         * @param {string} param.category - key category
         */
        keySideEffect({ definitions, category }) {
            const {
                immutableUpdate,
                lodash: { isEqual },
            } = this.$helpers

            const { primaryKey, uniqueKey } = this.CREATE_TBL_TOKENS
            const { PK, UQ } = this.COL_ATTRS
            let keyToken, initialKeyColNames, initialKeys
            switch (category) {
                case PK:
                    keyToken = primaryKey
                    initialKeyColNames = this.initialPkColNames
                    initialKeys = this.initialPkKeys
                    break
                case UQ:
                    keyToken = uniqueKey
                    initialKeyColNames = this.initialUqColNames
                    initialKeys = this.initialUqKeys
                    break
            }

            const stagingKeyColNames = queryHelper.getColNamesByAttr({
                cols: definitions.cols,
                attr: category,
            })

            // when all keys in a category key are removed
            if (!stagingKeyColNames.length)
                return immutableUpdate(definitions, { keys: { $unset: [keyToken] } })
            // If there are no changes
            else if (isEqual(initialKeyColNames, stagingKeyColNames))
                return immutableUpdate(definitions, {
                    keys: { $merge: { [keyToken]: initialKeys } },
                })
            // If there are changes in quantity of keys
            switch (category) {
                case PK:
                    return immutableUpdate(definitions, {
                        keys: {
                            [keyToken]: {
                                [0]: {
                                    ['index_cols']: {
                                        $set: stagingKeyColNames.map(c => ({ name: c })),
                                    },
                                },
                            },
                        },
                    })
                case UQ: {
                    return immutableUpdate(definitions, {
                        keys: { $merge: { [keyToken]: this.genUqKeys(stagingKeyColNames) } },
                    })
                }
            }
        },
        genUqKeys(colNames) {
            return colNames.map(name => {
                const existingKey = queryHelper.getKeyObjByColNames({
                    keys: this.initialKeys,
                    keyType: this.CREATE_TBL_TOKENS.uniqueKey,
                    colNames: [name],
                })
                if (existingKey) return existingKey
                // generate new one
                return {
                    category: this.CREATE_TBL_TOKENS.uniqueKey,
                    index_cols: [{ name }],
                    name: queryHelper.genUqName(name),
                }
            })
        },
    },
}
</script>
