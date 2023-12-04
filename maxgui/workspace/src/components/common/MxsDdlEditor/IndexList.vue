<template>
    <mxs-virtual-scroll-tbl
        :headers="headers"
        :data="keyItems"
        :itemHeight="32"
        :maxHeight="dim.height"
        :boundingWidth="dim.width"
        showSelect
        singleSelect
        :noDataText="$mxs_t('noEntity', { entityName: $mxs_t('indexes') })"
        :style="{ width: `${dim.width}px` }"
        :selectedItems.sync="selectedRows"
        @row-click="onRowClick"
    >
        <template
            v-for="h in headers"
            v-slot:[h.text]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <lazy-select
                v-if="h.text === KEY_EDITOR_ATTRS.CATEGORY"
                :key="h.text"
                :value="cell"
                :name="h.text"
                :height="28"
                :items="categories"
                :disabled="isInputDisabled({ field: h.text, rowData })"
                :selectionText="categoryTxt(cell)"
                @on-input="onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })"
                @on-focused="onRowClick(rowData)"
            />
            <lazy-text-field
                v-else
                :key="h.text"
                :value="cell"
                :name="h.text"
                :height="28"
                :required="isInputRequired(h.text)"
                :disabled="isInputDisabled({ field: h.text, rowData })"
                @on-input="onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })"
                @on-focused="onRowClick(rowData)"
            />
        </template>
    </mxs-virtual-scroll-tbl>
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
import { mapState } from 'vuex'
import LazyTextField from '@wsSrc/components/common/MxsDdlEditor/LazyTextField'
import LazySelect from '@wsSrc/components/common/MxsDdlEditor/LazySelect'

export default {
    name: 'index-list',
    components: { LazyTextField, LazySelect },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        selectedItems: { type: Array, default: () => [] }, //sync
    },
    data() {
        return {
            keyItems: [],
            stagingCategoryMap: {},
        }
    },
    computed: {
        ...mapState({
            KEY_EDITOR_ATTRS: state => state.mxsWorkspace.config.KEY_EDITOR_ATTRS,
            KEY_EDITOR_ATTR_IDX_MAP: state => state.mxsWorkspace.config.KEY_EDITOR_ATTR_IDX_MAP,
            NON_FK_CATEGORIES: state => state.mxsWorkspace.config.NON_FK_CATEGORIES,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
        }),
        idxOfId() {
            return this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.ID]
        },
        idxOfCategory() {
            return this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.CATEGORY]
        },
        headers() {
            const { ID, NAME, CATEGORY, COMMENT } = this.KEY_EDITOR_ATTRS
            const header = { sortable: false, uppercase: true, useCellSlot: true }
            return [
                { text: ID, hidden: true },
                { text: NAME, minWidth: 90, required: true, ...header },
                { text: CATEGORY, width: 145, required: true, minWidth: 145, ...header },
                { text: COMMENT, minWidth: 200, ...header },
            ]
        },
        keyCategoryMap: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        selectedRows: {
            get() {
                return this.selectedItems
            },
            set(v) {
                this.$emit('update:selectedItems', v)
            },
        },
        hasPk() {
            return Boolean(this.stagingCategoryMap[this.CREATE_TBL_TOKENS.primaryKey])
        },
        categories() {
            return Object.values(this.NON_FK_CATEGORIES).map(item => {
                return {
                    text: this.categoryTxt(item),
                    value: item,
                    disabled: item === this.CREATE_TBL_TOKENS.primaryKey && this.hasPk,
                }
            })
        },
    },
    watch: {
        keyCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingCategoryMap)) this.assignData()
            },
        },
        stagingCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keyCategoryMap, v)) this.keyCategoryMap = v
            },
        },
    },
    created() {
        this.init()
    },
    methods: {
        init() {
            this.assignData()
            this.handleSelectItem(0)
        },
        assignData() {
            this.stagingCategoryMap = this.$helpers.lodash.cloneDeep(this.keyCategoryMap)
            const { foreignKey, primaryKey } = this.CREATE_TBL_TOKENS
            this.keyItems = Object.values(this.NON_FK_CATEGORIES).reduce((acc, category) => {
                if (category !== foreignKey) {
                    const keys = Object.values(this.stagingCategoryMap[category] || {})
                    acc = [
                        ...acc,
                        ...keys.map(({ id, name = this.categoryTxt(primaryKey), comment = '' }) => [
                            id,
                            name,
                            category,
                            comment,
                        ]),
                    ]
                }
                return acc
            }, [])
        },
        handleSelectItem(idx) {
            if (this.keyItems.length) this.selectedRows = [this.keyItems.at(idx)]
        },
        categoryTxt(category) {
            if (category === this.CREATE_TBL_TOKENS.key) return 'INDEX'
            return category.replace('KEY', '')
        },
        isInputRequired(field) {
            const { COMMENT } = this.KEY_EDITOR_ATTRS
            return field !== COMMENT
        },
        isInputDisabled({ field, rowData }) {
            const { COMMENT } = this.KEY_EDITOR_ATTRS
            const category = rowData[this.idxOfCategory]
            return category === this.CREATE_TBL_TOKENS.primaryKey && field !== COMMENT
        },
        onChangeInput({ field, rowData, rowIdx, colIdx, value }) {
            const category = rowData[this.idxOfCategory]
            const keyId = rowData[this.idxOfId]
            const { NAME, CATEGORY, COMMENT } = this.KEY_EDITOR_ATTRS

            let currKeyMap = this.stagingCategoryMap[category] || {}
            const clonedKey = this.$helpers.lodash.cloneDeep(currKeyMap[keyId])

            switch (field) {
                case NAME:
                    this.stagingCategoryMap = this.$helpers.immutableUpdate(
                        this.stagingCategoryMap,
                        {
                            [category]: { [keyId]: { name: { $set: value } } },
                        }
                    )
                    break
                case COMMENT:
                    this.stagingCategoryMap = this.$helpers.immutableUpdate(
                        this.stagingCategoryMap,
                        {
                            [category]: {
                                [keyId]: value
                                    ? { comment: { $set: value } }
                                    : { $unset: ['comment'] },
                            },
                        }
                    )
                    break
                case CATEGORY: {
                    currKeyMap = this.$helpers.immutableUpdate(currKeyMap, { $unset: [keyId] })
                    const newCategory = value
                    let keyCategoryMap = this.$helpers.immutableUpdate(
                        this.stagingCategoryMap,
                        Object.keys(currKeyMap).length
                            ? { $merge: { [category]: currKeyMap } }
                            : { $unset: [category] }
                    )
                    const newCategoryKeyMap = keyCategoryMap[newCategory] || {}
                    keyCategoryMap = this.$helpers.immutableUpdate(keyCategoryMap, {
                        $merge: {
                            [newCategory]: {
                                ...newCategoryKeyMap,
                                [clonedKey.id]: clonedKey,
                            },
                        },
                    })
                    this.stagingCategoryMap = keyCategoryMap
                    break
                }
            }

            // Update component states
            this.keyItems = this.$helpers.immutableUpdate(this.keyItems, {
                [rowIdx]: { [colIdx]: { $set: value } },
            })

            /* TODO: Resolve the current limitation in mxs-virtual-scroll-tbl.
             * The `selectedItems` props stores the entire row data, so that
             * the styles for selected rows can be applied.
             * The id of the row should have been used to facilitate the edit
             * feature. For now, selectedRows must be also updated.
             */
            const isUpdatingIdxRow =
                this.$typy(this.selectedRows, `[0][${this.idxOfId}]`).safeString === keyId
            if (isUpdatingIdxRow)
                this.selectedRows = this.$helpers.immutableUpdate(this.selectedRows, {
                    [0]: { [colIdx]: { $set: value } },
                })
        },
        onRowClick(rowData) {
            this.selectedRows = [rowData]
        },
    },
}
</script>
