<template>
    <div class="fill-height">
        <tbl-toolbar
            :selectedItems="selectedItems"
            :isVertTable.sync="isVertTable"
            @get-computed-height="headerHeight = $event"
            @on-delete-selected-items="deleteSelectedRows"
            @on-add="addNewCol"
        >
            <template v-slot:append>
                <mxs-filter-list
                    v-model="selectedColSpecs"
                    activatorClass="ml-2"
                    returnObject
                    :label="$mxs_t('specs')"
                    :items="colSpecs"
                    :maxHeight="tableMaxHeight - 20"
                />
            </template>
        </tbl-toolbar>
        <mxs-virtual-scroll-tbl
            :headers="visHeaders"
            :rows="cols"
            :itemHeight="32"
            :maxHeight="tableMaxHeight"
            :boundingWidth="dim.width"
            showSelect
            :isVertTable="isVertTable"
            v-on="$listeners"
            @selected-rows="selectedItems = $event"
        >
            <template v-for="name in requiredHeaders" v-slot:[`header-${name}`]>
                <span :key="name" class="label-required">{{ name }}</span>
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
import TblToolbar from '@wsSrc/components/common/MxsDdlEditor/TblToolbar.vue'
import {
    getColumnTypes,
    checkCharsetSupport,
    checkUniqueZeroFillSupport,
    checkAutoIncrementSupport,
} from '@wsSrc/components/common/MxsDdlEditor/utils.js'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'col-definitions',
    components: { ColOptInput, TblToolbar },
    props: {
        value: { type: Object, required: true },
        initialData: { type: Object, required: true },
        dim: { type: Object, required: true },
        defTblCharset: { type: String, required: true },
        defTblCollation: { type: String, required: true },
        charsetCollationMap: { type: Object, required: true },
    },
    data() {
        return {
            selectedItems: [],
            isVertTable: false,
            selectedColSpecs: [],
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            GENERATED_TYPES: state => state.mxsWorkspace.config.GENERATED_TYPES,
        }),
        tableMaxHeight() {
            return this.dim.height - this.headerHeight
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
        requiredHeaders() {
            const { NAME, TYPE } = this.COL_ATTRS
            return [NAME, TYPE]
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
        idxOfColId() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ID]
        },
        idxOfColName() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NAME]
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
        initialCols() {
            return this.$typy(this.initialData, 'cols').safeArray
        },
        initialPkCols() {
            return this.$typy(this.initialKeys, `[${this.CREATE_TBL_TOKENS.primaryKey}][0].cols`)
                .safeArray
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
            let definitions = this.$helpers.immutableUpdate(this.definitions, {
                cols: {
                    $set: xorWith(this.definitions.cols, selectedItems, isEqual),
                },
            })
            /* All associated columns in keys also need to be deleted.
             * When a column is deleted, the composite key
             * (except PK) needs to be altered. i.e. removing the column from cols.
             * The key is dropped if cols is empty.
             */
            selectedItems.forEach(col => {
                const keyTypes = queryHelper.findKeyTypesByColId({
                    keys: definitions.keys,
                    colId: col[this.idxOfColId],
                })
                keyTypes.forEach(category => {
                    definitions = this.keySideEffect({
                        definitions,
                        category,
                        col,
                        mode: 'delete',
                    })
                })
            })
            this.definitions = definitions
        },
        addNewCol() {
            let row = []
            const { ID, PK, NN, UN, UQ, ZF, AI, GENERATED_TYPE } = this.COL_ATTRS
            this.headers.forEach(h => {
                switch (h.text) {
                    case ID:
                        row.push(`col_${this.$helpers.uuidv1()}`)
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
            const alterColIdx = item.alterColIdx
            let definitions = this.$helpers.immutableUpdate(this.definitions, {
                cols: {
                    [alterColIdx]: {
                        [item.colOptIdx]: { $set: item.value },
                    },
                },
            })
            const { TYPE, PK, NN, UQ, AI, GENERATED_TYPE, CHARSET } = this.COL_ATTRS

            const col = Object.values(item.rowObj)

            switch (item.field) {
                case TYPE:
                    definitions = this.onChangeType({ definitions, item })
                    break
                case PK:
                case UQ:
                    if (item.field === PK) definitions = this.onTogglePk({ definitions, item })
                    definitions = this.keySideEffect({
                        definitions,
                        category: item.field,
                        col,
                        mode: item.value ? 'add' : 'drop',
                    })
                    break
                case NN:
                    definitions = this.notNullSideEffect({
                        definitions,
                        alterColIdx,
                        isNN: item.value,
                    })
                    break
                case AI:
                    definitions = this.onToggleAi({ definitions, item })
                    break
                case GENERATED_TYPE:
                    definitions = this.$helpers.immutableUpdate(definitions, {
                        cols: {
                            [alterColIdx]: {
                                [this.idxOfAI]: { $set: false },
                                [this.idxOfNN]: { $set: false },
                            },
                        },
                    })
                    break
                case CHARSET:
                    definitions = this.patchCharsetCollation({
                        definitions,
                        alterColIdx,
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
            if (!checkUniqueZeroFillSupport(item.value) || !checkAutoIncrementSupport(item.value)) {
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
            if (checkCharsetSupport(item.value)) {
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
            const alterColIdx = item.alterColIdx
            if (item.value === 'SERIAL') {
                defs = this.uncheckOtherAI({ definitions: defs, alterColIdx })
                defs = this.$helpers.immutableUpdate(defs, {
                    cols: {
                        [alterColIdx]: {
                            [this.idxOfUN]: { $set: true },
                            [this.idxOfNN]: { $set: true },
                            [this.idxOfAI]: { $set: true },
                            [this.idxOfUQ]: { $set: true },
                        },
                    },
                })
                defs = this.keySideEffect({
                    definitions: defs,
                    col: Object.values(item.rowObj),
                    category: this.CREATE_TBL_TOKENS.uniqueKey,
                    mode: 'add',
                })
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
            const alterColIdx = item.alterColIdx
            // update UQ key value
            defs = this.$helpers.immutableUpdate(defs, {
                cols: { [alterColIdx]: { [this.idxOfUQ]: { $set: false } } },
            })
            defs = this.keySideEffect({
                definitions: defs,
                col: Object.values(item.rowObj),
                category: this.CREATE_TBL_TOKENS.uniqueKey,
                mode: 'drop',
            })
            defs = this.notNullSideEffect({
                definitions: defs,
                alterColIdx,
                isNN: true,
            })
            return defs
        },
        /**
         * @param {object} param.definitions - parsed definitions
         * @param {string} param.colId - col id
         * @returns {object} new definitions object
         */
        updatePk({ definitions, colId, mode }) {
            const { primaryKey } = this.CREATE_TBL_TOKENS
            const {
                immutableUpdate,
                lodash: { cloneDeep, sortBy, isEqual },
            } = this.$helpers
            const category = primaryKey
            // Get PK object.
            let pkObj
            if (definitions.keys[primaryKey]) {
                // PK category always has one object if a table has PK,
                pkObj = cloneDeep(definitions.keys[primaryKey][0])
            } else pkObj = { cols: [] }

            switch (mode) {
                case 'drop': {
                    const targetIndex = pkObj.cols.findIndex(c => c.id === colId)
                    if (targetIndex >= 0) pkObj.cols.splice(targetIndex, 1)
                    break
                }
                case 'add':
                    pkObj.cols.push({ id: colId })
                    break
            }

            if (isEqual(sortBy(pkObj.cols, ['id']), sortBy(this.initialPkCols, ['id'])))
                pkObj.cols = this.initialPkCols

            if (!pkObj.id) {
                const existingKey = this.getKeyObjByColId({
                    keys: this.initialKeys,
                    category,
                    colId,
                    isCompositeKey: true,
                })
                pkObj.id = existingKey ? existingKey.id : `key_${this.$helpers.uuidv1()}`
            }

            return immutableUpdate(definitions, {
                keys: pkObj.cols.length
                    ? { $merge: { [primaryKey]: [pkObj] } }
                    : { $unset: [primaryKey] },
            })
        },
        /**
         * @param {Object} param
         * @param {object} param.keys - parsed keys from DDL of a table
         * @param {string} param.category - category of the key
         * @param {string} param.colId - column id to be looked up
         * @param {boolean} param.isCompositeKey - return the key object if at least one col in
         * cols matches with the provided column id.
         * @returns {object} index object
         */
        getKeyObjByColId({ keys, category, colId, isCompositeKey }) {
            return this.$typy(keys, `[${category}]`).safeArray.find(key => {
                if (isCompositeKey) return key.cols.some(col => col.id === colId)
                return key.cols.every(col => col.id === colId)
            })
        },
        genKey({ definitions, category, colId }) {
            const existingKey = this.getKeyObjByColId({ keys: this.initialKeys, category, colId })
            if (existingKey) return existingKey
            const col = definitions.cols.find(c => c[this.idxOfColId] === colId)
            const colName = col[this.idxOfColName]
            return {
                id: `key_${this.$helpers.uuidv1()}`,
                cols: [{ id: colId }],
                name: queryHelper.genKeyName({ colName, category }),
            }
        },
        /**
         * @param {object} param.definitions - parsed definitions
         * @param {string} param.category - uniqueKey, fullTextKey, spatialKey, key or foreignKey
         * @param {string} param.colId - col id
         * @returns {object} new definitions object
         */
        updateKey({ definitions, category, colId, mode }) {
            const {
                immutableUpdate,
                lodash: { cloneDeep, sortBy, isEqual },
            } = this.$helpers
            let keys = cloneDeep(definitions.keys[category]) || []
            switch (mode) {
                case 'drop':
                    keys = keys.filter(keyObj => !keyObj.cols.every(c => c.id === colId))
                    break
                case 'delete': {
                    keys = keys.reduce((acc, key) => {
                        const targetIndex = key.cols.findIndex(c => c.id === colId)
                        if (targetIndex >= 0) key.cols.splice(targetIndex, 1)
                        if (key.cols.length) acc.push(key)
                        return acc
                    }, [])
                    break
                }
                case 'add':
                    keys.push(this.genKey({ definitions, category, colId }))
                    break
            }
            /**
             * Check the order of the keys, if it equals to initial keys, use that
             */
            const initialKeys = this.initialKeys[category] || []
            if (isEqual(sortBy(keys, ['name']), sortBy(initialKeys, ['name']))) keys = initialKeys
            // If there is no key, remove that category from definitions.keys
            return immutableUpdate(definitions, {
                keys: keys.length ? { $merge: { [category]: keys } } : { $unset: [category] },
            })
        },
        /**
         * @param {object} param.definitions - column definitions
         * @param {string} param.category - key category
         * @param {array} param.col - column data before updating
         * @param {string} param.mode - add|drop|delete. delete mode should be used
         * only after dropping a column as it's reserved for handling composite keys.
         * The column in the composite key objects will be deleted.
         */
        keySideEffect({ definitions, category, col, mode }) {
            const colId = col[this.idxOfColId]
            const {
                primaryKey,
                uniqueKey,
                fullTextKey,
                spatialKey,
                key,
                foreignKey,
            } = this.CREATE_TBL_TOKENS
            switch (category) {
                case primaryKey:
                    return this.updatePk({ definitions, colId, mode })
                case uniqueKey:
                case fullTextKey:
                case spatialKey:
                case key:
                case foreignKey:
                    return this.updateKey({
                        definitions,
                        category,
                        colId,
                        mode,
                    })
                default:
                    return definitions
            }
        },
    },
}
</script>
