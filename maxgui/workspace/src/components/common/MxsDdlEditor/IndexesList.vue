<template>
    <mxs-virtual-scroll-tbl
        :headers="headers"
        :rows="keyItems"
        :itemHeight="32"
        :maxHeight="dim.height"
        :boundingWidth="dim.width"
        showSelect
        singleSelect
        :noDataText="$mxs_t('noEntity', { entityName: $mxs_t('indexes') })"
        :style="{ width: `${dim.width}px` }"
        :selectedItems.sync="selectedItems"
        @row-click="onRowClick"
    >
        <template
            v-for="h in headers"
            v-slot:[h.text]="{ data: { cell, rowIdx, colIdx, rowData } }"
        >
            <v-select
                v-if="h.text === KEY_EDITOR_ATTRS.CATEGORY"
                :key="h.text"
                :value="cell"
                class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                :items="categories"
                outlined
                dense
                :height="28"
                hide-details
                :disabled="isInputDisabled({ field: h.text, rowData })"
                @input="onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })"
            />
            <mxs-debounced-field
                v-else
                :key="h.text"
                :value="cell"
                class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
                single-line
                outlined
                dense
                :height="28"
                hide-details
                :required="isInputRequired(h.text)"
                :disabled="isInputDisabled({ field: h.text, rowData })"
                :rules="isInputRequired(h.text) ? [v => !!v] : []"
                @input="onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'indexes-list',
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        selectedItem: { type: Array, default: () => [] }, //sync
    },
    data() {
        return {
            keyItems: [],
            stagingKeys: {},
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
            const header = { sortable: false, uppercase: true }
            return [
                { text: ID, hidden: true },
                { text: NAME, minWidth: 90, required: true, ...header },
                { text: CATEGORY, width: 145, required: true, minWidth: 145, ...header },
                { text: COMMENT, minWidth: 200, ...header },
            ]
        },
        keys: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        selectedItems: {
            get() {
                if (this.$typy(this.selectedItem).isEmptyObject) return []
                return [this.selectedItem]
            },
            set(v) {
                this.$emit('update:selectedItem', this.$typy(v, '[0]').safeArray)
            },
        },
        hasPk() {
            return Boolean(this.stagingKeys[this.CREATE_TBL_TOKENS.primaryKey])
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
        stagingKeys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keys, v)) this.keys = v
            },
        },
    },
    created() {
        this.assignData()
    },
    methods: {
        assignData() {
            this.stagingKeys = this.$helpers.lodash.cloneDeep(this.keys)
            const { foreignKey, primaryKey } = this.CREATE_TBL_TOKENS
            this.keyItems = Object.values(this.NON_FK_CATEGORIES).reduce((acc, category) => {
                if (category !== foreignKey) {
                    const keys = Object.values(this.stagingKeys[category] || {})
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
            if (this.keyItems.length) this.selectedItems = [this.keyItems[0]]
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

            let currKeyMap = this.stagingKeys[category] || {}
            const clonedKey = this.$helpers.lodash.cloneDeep(currKeyMap[keyId])

            switch (field) {
                case NAME:
                    this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                        [category]: { [keyId]: { name: { $set: value } } },
                    })
                    break
                case COMMENT:
                    this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                        [category]: {
                            [keyId]: value ? { comment: { $set: value } } : { $unset: ['comment'] },
                        },
                    })
                    break
                case CATEGORY: {
                    currKeyMap = this.$helpers.immutableUpdate(currKeyMap, { $unset: [keyId] })
                    const newCategory = value
                    let keys = this.$helpers.immutableUpdate(
                        this.stagingKeys,
                        Object.keys(currKeyMap).length
                            ? { $merge: { [category]: currKeyMap } }
                            : { $unset: [category] }
                    )
                    const newCategoryKeyMap = keys[newCategory] || {}
                    keys = this.$helpers.immutableUpdate(keys, {
                        $merge: {
                            [newCategory]: {
                                ...newCategoryKeyMap,
                                [clonedKey.id]: clonedKey,
                            },
                        },
                    })
                    this.stagingKeys = keys
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
             * feature. For now, selectedItems must be also updated.
             */
            const isUpdatingSelectedItem =
                this.$typy(this.selectedItems, `[0][${this.idxOfId}]`).safeString === keyId
            if (isUpdatingSelectedItem)
                this.selectedItems = this.$helpers.immutableUpdate(this.selectedItems, {
                    [0]: { [colIdx]: { $set: value } },
                })
        },
        onRowClick(rowData) {
            this.selectedItems = [rowData]
        },
    },
}
</script>
