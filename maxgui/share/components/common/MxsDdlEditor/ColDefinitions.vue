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
                    v-model="hiddenColSpecs"
                    reverse
                    activatorClass="ml-2"
                    :label="$mxs_t('specs')"
                    :items="colSpecs"
                    :maxHeight="tableMaxHeight - 20"
                />
            </template>
        </tbl-toolbar>
        <mxs-virtual-scroll-tbl
            :headers="headers"
            :data="rows"
            :itemHeight="32"
            :maxHeight="tableMaxHeight"
            :boundingWidth="dim.width"
            showSelect
            :isVertTable="isVertTable"
            :selectedItems.sync="selectedItems"
            v-on="$listeners"
        >
            <template
                v-for="(value, key) in abbreviatedHeaders"
                v-slot:[`header-${key}`]="{ data: { header, maxWidth } }"
            >
                <v-tooltip :key="key" top transition="slide-y-transition" :disabled="isVertTable">
                    <template v-slot:activator="{ on }">
                        <div
                            class="d-inline-block text-truncate text-uppercase"
                            :style="{ maxWidth: `${maxWidth}px` }"
                            v-on="on"
                        >
                            {{ isVertTable ? value : header.text }}
                        </div>
                    </template>
                    <span>{{ value }}</span>
                </v-tooltip>
            </template>
            <template v-slot:[`header-${COL_ATTRS.DEF_EXP}`]>
                DEFAULT/EXPRESSION
            </template>
            <template v-slot:[COL_ATTRS.TYPE]="{ data: { rowData, cell } }">
                <div class="fill-height d-flex align-center">
                    <data-type-input
                        :value="cell"
                        :height="28"
                        :items="dataTypes"
                        @on-input="onChangeInput({ value: $event, rowData, field: COL_ATTRS.TYPE })"
                    />
                </div>
            </template>
            <template v-slot:[COL_ATTRS.GENERATED]="{ data: { rowData, cell } }">
                <div class="fill-height d-flex align-center">
                    <!-- disable if column is PK
                    https://mariadb.com/kb/en/generated-columns/#index-support -->
                    <lazy-select
                        :value="cell"
                        :height="28"
                        :name="COL_ATTRS.GENERATED"
                        :items="generatedTypeItems"
                        :disabled="isPkRow(rowData)"
                        @on-input="
                            onChangeInput({ value: $event, rowData, field: COL_ATTRS.GENERATED })
                        "
                    />
                </div>
            </template>
            <template v-for="txtField in txtFields" v-slot:[txtField]="{ data: { rowData, cell } }">
                <lazy-text-field
                    :key="txtField"
                    :name="txtField"
                    :value="cell"
                    :height="28"
                    :required="txtField === COL_ATTRS.NAME"
                    @on-input="onChangeInput({ value: $event, rowData, field: txtField })"
                />
            </template>
            <template
                v-for="boolField in boolFields"
                v-slot:[boolField]="{ data: { rowData, cell } }"
            >
                <bool-input
                    :key="boolField"
                    :value="cell"
                    :rowData="rowData"
                    :field="boolField"
                    :height="28"
                    @on-input="onChangeInput({ value: $event, rowData, field: boolField })"
                />
            </template>
            <template
                v-for="field in charsetCollateFields"
                v-slot:[field]="{ data: { rowData, cell } }"
            >
                <charset-collate-input
                    :key="field"
                    :value="cell"
                    :rowData="rowData"
                    :field="field"
                    :height="28"
                    :charsetCollationMap="charsetCollationMap"
                    :defTblCharset="defTblCharset"
                    :defTblCollation="defTblCollation"
                    @on-input="onChangeInput({ value: $event, rowData, field })"
                />
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LazyTextField from '@share/components/common/MxsDdlEditor/LazyTextField'
import DataTypeInput from '@share/components/common/MxsDdlEditor/DataTypeInput'
import LazySelect from '@share/components/common/MxsDdlEditor/LazySelect'
import BoolInput from '@share/components/common/MxsDdlEditor/BoolInput'
import CharsetCollateInput from '@share/components/common/MxsDdlEditor/CharsetCollateInput'
import TblToolbar from '@share/components/common/MxsDdlEditor/TblToolbar.vue'
import {
    getColumnTypes,
    checkUniqueZeroFillSupport,
    checkAutoIncrementSupport,
} from '@share/components/common/MxsDdlEditor/utils.js'
import erdHelper from '@wsSrc/utils/erdHelper'
import { CREATE_TBL_TOKENS, COL_ATTRS, COL_ATTRS_IDX_MAP, GENERATED_TYPES } from '@wsSrc/constants'

export default {
    name: 'col-definitions',
    components: {
        LazyTextField,
        DataTypeInput,
        LazySelect,
        BoolInput,
        CharsetCollateInput,
        TblToolbar,
    },
    props: {
        value: { type: Object, required: true },
        initialData: { type: Object, required: true },
        dim: { type: Object, required: true },
        defTblCharset: { type: String, required: true },
        defTblCollation: { type: String, required: true },
        charsetCollationMap: { type: Object, required: true },
        colKeyCategoryMap: { type: Object, required: true },
    },
    data() {
        return {
            selectedItems: [],
            isVertTable: false,
            hiddenColSpecs: [],
            headerHeight: 0,
        }
    },
    computed: {
        tableMaxHeight() {
            return this.dim.height - this.headerHeight
        },
        defs: {
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
        colSpecs() {
            return this.colAttrs.filter(attr => attr !== this.COL_ATTRS.ID)
        },
        headers() {
            const { ID, NAME, TYPE, PK, NN, UN, UQ, ZF, AI, GENERATED } = this.COL_ATTRS
            return this.colAttrs.map(field => {
                let h = {
                    text: field,
                    sortable: false,
                    uppercase: true,
                    hidden: this.hiddenColSpecs.includes(field),
                    useCellSlot: true,
                }
                switch (field) {
                    case NAME:
                    case TYPE:
                        h.required = true
                        break
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
                    case GENERATED:
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
        txtFields() {
            const { NAME, DEF_EXP, COMMENT } = this.COL_ATTRS
            return [NAME, DEF_EXP, COMMENT]
        },
        boolFields() {
            const { PK, NN, UN, UQ, ZF, AI } = this.COL_ATTRS
            return [PK, NN, UN, UQ, ZF, AI]
        },
        charsetCollateFields() {
            const { CHARSET, COLLATE } = this.COL_ATTRS
            return [CHARSET, COLLATE]
        },
        abbreviatedHeaders() {
            const { PK, NN, UN, UQ, ZF, AI, GENERATED } = this.COL_ATTRS
            const { primaryKey, nn, un, uniqueKey, zf, ai } = CREATE_TBL_TOKENS
            return {
                [PK]: primaryKey,
                [NN]: nn,
                [UN]: un,
                [UQ]: uniqueKey,
                [ZF]: zf,
                [AI]: ai,
                [GENERATED]: 'GENERATED',
            }
        },
        cols() {
            return Object.values(this.defs.col_map || {})
        },
        transformedCols() {
            const {
                ID,
                NAME,
                TYPE,
                PK,
                NN,
                UN,
                UQ,
                ZF,
                AI,
                GENERATED,
                DEF_EXP,
                CHARSET,
                COLLATE,
                COMMENT,
            } = this.COL_ATTRS
            const tokens = CREATE_TBL_TOKENS

            return this.cols.map(col => {
                let type = col.data_type
                if (col.data_type_size) type += `(${col.data_type_size})`
                const categories = this.colKeyCategoryMap[col.id] || []

                let uq = false
                if (categories.includes(tokens.uniqueKey)) {
                    /**
                     * UQ input is a checkbox for a column, so it can't handle composite unique
                     * key. Thus ignoring composite unique key.
                     */
                    uq = erdHelper.isSingleUQ({
                        keyCategoryMap: this.$typy(this.defs, 'key_category_map').safeObjectOrEmpty,
                        colId: col.id,
                    })
                }
                return {
                    [ID]: col.id,
                    [NAME]: col.name,
                    [TYPE]: type,
                    [PK]: categories.includes(tokens.primaryKey),
                    [NN]: col.nn,
                    [UN]: col.un,
                    [UQ]: uq,
                    [ZF]: col.zf,
                    [AI]: col.ai,
                    [GENERATED]: col.generated ? col.generated : GENERATED_TYPES.NONE,
                    [DEF_EXP]: col.default_exp,
                    [CHARSET]: this.$typy(col.charset).safeString,
                    [COLLATE]: this.$typy(col.collate).safeString,
                    [COMMENT]: this.$typy(col.comment).safeString,
                }
            })
        },
        rows() {
            return this.transformedCols.map(col => [...Object.values(col)])
        },
        dataTypes() {
            let items = []
            getColumnTypes().forEach(item => {
                // place header first, then all its types and add a divider
                items = [...items, { header: item.header }, ...item.types, { divider: true }]
            })
            return items
        },
        autoIncrementCol() {
            return this.cols.find(col => col.ai)
        },
        initialKeyCategoryMap() {
            return this.$typy(this.initialData, 'key_category_map').safeObjectOrEmpty
        },
        stagingKeyCategoryMap() {
            return this.$typy(this.defs, 'key_category_map').safeObjectOrEmpty
        },
        initialPk() {
            return this.$typy(
                Object.values(this.initialKeyCategoryMap[CREATE_TBL_TOKENS.primaryKey] || {}),
                `[0]`
            ).safeObject
        },
        initialPkCols() {
            return this.$typy(this.initialPk, 'cols').safeArray
        },
        stagingPk() {
            return this.$typy(
                Object.values(this.stagingKeyCategoryMap[CREATE_TBL_TOKENS.primaryKey] || {}),
                `[0]`
            ).safeObject
        },
        generatedTypeItems() {
            return Object.values(GENERATED_TYPES)
        },
    },
    created() {
        this.COL_ATTRS = COL_ATTRS
        this.handleShowColSpecs()
    },
    methods: {
        handleShowColSpecs() {
            if (this.$vuetify.breakpoint.width >= 1680) this.hiddenColSpecs = []
            else {
                const { CHARSET, COLLATE, COMMENT } = this.COL_ATTRS
                this.hiddenColSpecs = [CHARSET, COLLATE, COMMENT]
            }
        },
        deleteSelectedRows(selectedItems) {
            const selectedIds = selectedItems.map(row => row[0])
            let defs = this.$helpers.immutableUpdate(this.defs, {
                col_map: { $unset: selectedIds },
            })
            /* All associated columns in keys also need to be deleted.
             * When a column is deleted, the composite key needs to be modified. i.e.
             * The cols array must remove the selected ids
             * The key is dropped if cols is empty.
             */
            selectedIds.forEach(id => {
                const categories = this.colKeyCategoryMap[id] || []
                categories.forEach(category => {
                    defs = this.keySideEffect({
                        defs,
                        category,
                        colId: id,
                        mode: 'delete',
                    })
                })
            })
            this.defs = defs
        },
        addNewCol() {
            const col = {
                name: 'name',
                data_type: 'CHAR',
                un: false,
                zf: false,
                nn: false,
                charset: undefined,
                collate: undefined,
                generated: undefined,
                ai: false,
                default_exp: CREATE_TBL_TOKENS.null,
                comment: undefined,
                id: `col_${this.$helpers.uuidv1()}`,
            }
            this.defs = this.$helpers.immutableUpdate(this.defs, {
                col_map: { $merge: { [col.id]: col } },
            })
        },
        /**
         * @param {object} param
         * @param {string|boolean} param.value - cell value
         * @param {array} param.rowData - row data
         * @param {string} param.field - field name
         */
        onChangeInput({ value, rowData, field }) {
            let defs = this.$helpers.lodash.cloneDeep(this.defs)
            const { ID, TYPE, PK, NN, UQ, AI, GENERATED, CHARSET } = this.COL_ATTRS
            const colId = rowData[COL_ATTRS_IDX_MAP[ID]]
            const param = { defs, colId, value }
            switch (field) {
                case TYPE:
                    defs = this.onChangeType(param)
                    break
                case PK:
                case UQ: {
                    const { uniqueKey, primaryKey } = CREATE_TBL_TOKENS
                    if (field === PK) defs = this.onTogglePk(param)
                    defs = this.keySideEffect({
                        defs,
                        category: field === PK ? primaryKey : uniqueKey,
                        colId,
                        mode: value ? 'add' : 'drop',
                    })
                    break
                }
                case NN:
                    defs = this.toggleNotNull(param)
                    break
                case AI:
                    defs = this.onToggleAI(param)
                    break
                case GENERATED:
                    defs = this.$helpers.immutableUpdate(defs, {
                        col_map: {
                            [colId]: {
                                [field]: { $set: value },
                                default_exp: { $set: '' },
                                ai: { $set: false },
                                nn: { $set: false },
                            },
                        },
                    })
                    break
                case CHARSET:
                    defs = this.setCharset(param)
                    break
                default: {
                    defs = this.$helpers.immutableUpdate(defs, {
                        col_map: { [colId]: { [field]: { $set: value || undefined } } },
                    })
                }
            }
            this.defs = defs
        },
        setCharset({ defs, colId, value }) {
            const charset = value === this.defTblCharset ? undefined : value || undefined
            const { defCollation } = this.charsetCollationMap[charset] || {}
            return this.$helpers.immutableUpdate(defs, {
                col_map: {
                    [colId]: {
                        charset: { $set: charset },
                        // also set a default collation
                        collate: { $set: defCollation },
                    },
                },
            })
        },
        uncheck_UN_ZF_AI({ defs, colId }) {
            return this.$helpers.immutableUpdate(defs, {
                col_map: {
                    [colId]: {
                        un: { $set: false },
                        zf: { $set: false },
                        ai: { $set: false },
                    },
                },
            })
        },
        setSerialType(param) {
            const { colId, value } = param
            let defs = param.defs
            defs = this.uncheckAI({ defs, colId, value })
            defs = this.$helpers.immutableUpdate(defs, {
                col_map: {
                    [colId]: {
                        un: { $set: true },
                        nn: { $set: true },
                        ai: { $set: true },
                    },
                },
            })
            defs = this.keySideEffect({
                defs,
                colId,
                category: CREATE_TBL_TOKENS.uniqueKey,
                mode: 'add',
            })
            return defs
        },
        onChangeType(param) {
            const { colId, value } = param
            let defs = this.$helpers.immutableUpdate(param.defs, {
                col_map: {
                    [colId]: {
                        data_type: { $set: value },
                        charset: { $set: undefined },
                        collate: { $set: undefined },
                    },
                },
            })
            if (value === 'SERIAL') defs = this.setSerialType({ defs, colId, value })
            if (!checkUniqueZeroFillSupport(value) || !checkAutoIncrementSupport(value))
                defs = this.uncheck_UN_ZF_AI({ defs, colId, value })
            return defs
        },
        /**
         * This uncheck auto_increment
         * @param {object} defs - current defs
         * @returns {Object} - returns new defs
         */
        uncheckAI(defs) {
            return (defs = this.$helpers.immutableUpdate(defs, {
                col_map: { [this.autoIncrementCol.id]: { ai: { $set: false } } },
            }))
        },
        /**
         * This updates NN cell and `default` cell.
         * @param {object} payload.defs - current defs
         * @param {string} payload.colIdx - column index
         * @param {boolean} payload.value
         * @returns {object} - returns new defs
         */
        toggleNotNull({ defs, colId, value }) {
            const { default_exp = CREATE_TBL_TOKENS.null } = this.$typy(
                this.initialData,
                `col_map[${colId}]`
            ).safeObjectOrEmpty
            return this.$helpers.immutableUpdate(defs, {
                col_map: {
                    [colId]: {
                        nn: { $set: value },
                        default_exp: { $set: value ? undefined : default_exp },
                    },
                },
            })
        },
        onToggleAI(param) {
            const { colId, value } = param
            let defs = param.defs
            if (this.autoIncrementCol) defs = this.uncheckAI(defs)

            defs = this.$helpers.immutableUpdate(defs, {
                col_map: {
                    [colId]: { ai: { $set: value }, generated: { $set: undefined } },
                },
            })

            return this.toggleNotNull({ defs, colId, value: true })
        },
        onTogglePk(param) {
            const { colId, value } = param
            let defs = param.defs
            defs = this.keySideEffect({
                defs,
                colId,
                category: CREATE_TBL_TOKENS.primaryKey,
                mode: value ? 'add' : 'drop',
            })
            defs = this.keySideEffect({
                defs,
                colId,
                category: CREATE_TBL_TOKENS.uniqueKey,
                mode: 'drop',
            })
            defs = this.toggleNotNull({
                defs,
                colId,
                value: true,
            })
            return defs
        },
        /**
         * @param {object} param.defs - parsed defs
         * @param {string} param.colId - col id
         * @returns {object} new defs object
         */
        updatePk({ defs, colId, mode }) {
            const { primaryKey } = CREATE_TBL_TOKENS
            const { immutableUpdate } = this.$helpers
            // Get PK object.
            let pkObj = this.stagingPk || {
                cols: [],
                id: this.initialPk ? this.initialPk.id : `key_${this.$helpers.uuidv1()}`,
            }

            switch (mode) {
                case 'delete':
                case 'drop': {
                    const targetIndex = pkObj.cols.findIndex(c => c.id === colId)
                    if (targetIndex >= 0) pkObj.cols.splice(targetIndex, 1)
                    break
                }
                case 'add':
                    pkObj.cols.push({ id: colId })
                    break
            }

            return immutableUpdate(defs, {
                key_category_map: pkObj.cols.length
                    ? { $merge: { [primaryKey]: { [pkObj.id]: pkObj } } }
                    : { $unset: [primaryKey] },
            })
        },
        genKey({ defs, category, colId }) {
            const existingKey = Object.values(this.initialKeyCategoryMap[category] || {}).find(
                key => {
                    return key.cols.every(col => col.id === colId)
                }
            )
            if (existingKey) return existingKey
            return erdHelper.genKey({ defs, category, colId })
        },
        /**
         * @param {object} param.defs - parsed defs
         * @param {string} param.category - uniqueKey, fullTextKey, spatialKey, key or foreignKey
         * @param {string} param.colId - col id
         * @returns {object} new defs object
         */
        updateKey({ defs, category, colId, mode }) {
            const {
                immutableUpdate,
                lodash: { cloneDeep },
            } = this.$helpers
            let keyMap = cloneDeep(defs.key_category_map[category]) || {}
            switch (mode) {
                case 'drop':
                    keyMap = immutableUpdate(keyMap, {
                        $unset: Object.values(keyMap).reduce((ids, k) => {
                            if (k.cols.every(c => c.id === colId)) ids.push(k.id)
                            return ids
                        }, []),
                    })
                    break
                case 'delete':
                    keyMap = immutableUpdate(keyMap, {
                        $set: Object.values(cloneDeep(keyMap)).reduce((obj, key) => {
                            const targetIndex = key.cols.findIndex(c => c.id === colId)
                            if (targetIndex >= 0) key.cols.splice(targetIndex, 1)
                            if (key.cols.length) obj[key.id] = key
                            return obj
                        }, {}),
                    })
                    break
                case 'add': {
                    const newKey = this.genKey({ defs, category, colId })
                    keyMap = immutableUpdate(keyMap, { $merge: { [newKey.id]: newKey } })
                    break
                }
            }
            return immutableUpdate(defs, {
                key_category_map: Object.keys(keyMap).length
                    ? { $merge: { [category]: keyMap } }
                    : { $unset: [category] },
            })
        },
        /**
         * @param {object} param.defs - column defs
         * @param {string} param.category - key category
         * @param {string} param.colId - column id
         * @param {string} param.mode - add|drop|delete. delete mode should be used
         * only after dropping a column as it's reserved for handling composite keys.
         * The column in the composite key objects will be deleted.
         */
        keySideEffect({ defs, category, colId, mode }) {
            const {
                primaryKey,
                uniqueKey,
                fullTextKey,
                spatialKey,
                key,
                foreignKey,
            } = CREATE_TBL_TOKENS
            switch (category) {
                case primaryKey:
                    return this.updatePk({ defs, colId, mode })
                case uniqueKey:
                case fullTextKey:
                case spatialKey:
                case key:
                case foreignKey:
                    return this.updateKey({
                        defs,
                        category,
                        colId,
                        mode,
                    })
                default:
                    return defs
            }
        },
        isPkRow(rowData) {
            return this.$typy(rowData, `[${COL_ATTRS_IDX_MAP[this.COL_ATTRS.PK]}]`).safeBoolean
        },
    },
}
</script>
