<template>
    <div class="fill-height">
        <tbl-toolbar
            :selectedItems="selectedItems"
            :showRotateTable="false"
            reverse
            @get-computed-height="headerHeight = $event"
            @on-delete-selected-items="deleteSelectedKeys"
            @on-add="addNewKey"
        />
        <div class="d-flex flex-row">
            <index-list
                ref="indexesList"
                v-model="stagingKeyCategoryMap"
                :dim="{ height: dim.height - headerHeight, width: keyTblWidth }"
                :selectedItems.sync="selectedItems"
                class="mr-4"
            />
            <index-col-list
                v-if="selectedKeyId"
                v-model="stagingKeyCategoryMap"
                :dim="{ height: dim.height - headerHeight, width: keyColsTblWidth }"
                :keyId="selectedKeyId"
                :category="selectedKeyCategory"
                :tableColNameMap="tableColNameMap"
                :tableColMap="tableColMap"
            />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TblToolbar from '@share/components/common/MxsDdlEditor/TblToolbar.vue'
import IndexList from '@share/components/common/MxsDdlEditor/IndexList.vue'
import IndexColList from '@share/components/common/MxsDdlEditor/IndexColList.vue'
import { CREATE_TBL_TOKENS, KEY_EDITOR_ATTRS, KEY_EDITOR_ATTR_IDX_MAP } from '@wsSrc/constants'

export default {
    name: 'index-definitions',
    components: { TblToolbar, IndexList, IndexColList },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        tableColNameMap: { type: Object, required: true },
        tableColMap: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItems: [],
            stagingKeyCategoryMap: {},
        }
    },
    computed: {
        idxOfId() {
            return KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.ID]
        },
        idxOfCategory() {
            return KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.CATEGORY]
        },
        keyTblWidth() {
            return this.dim.width / 2.25
        },
        keyColsTblWidth() {
            return this.dim.width - this.keyTblWidth - 16
        },
        keyCategoryMap: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        selectedItem() {
            return this.$typy(this.selectedItems, '[0]').safeArray
        },
        selectedKeyId() {
            const idx = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.ID]
            return this.selectedItem[idx]
        },
        selectedKeyCategory() {
            const idx = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.CATEGORY]
            return this.selectedItem[idx]
        },
    },
    watch: {
        keyCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingKeyCategoryMap)) {
                    this.init()
                    this.selectFirstItem()
                }
            },
        },
        stagingKeyCategoryMap: {
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
            this.stagingKeyCategoryMap = this.$helpers.lodash.cloneDeep(this.keyCategoryMap)
        },
        selectFirstItem() {
            this.selectedItems = []
            this.$nextTick(() => this.$refs.indexesList.handleSelectItem(0))
        },
        deleteSelectedKeys() {
            const item = this.selectedItem
            const id = item[this.idxOfId]
            const category = item[this.idxOfCategory]
            let keyMap = this.stagingKeyCategoryMap[category] || {}
            keyMap = this.$helpers.immutableUpdate(keyMap, {
                $unset: [id],
            })
            this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(
                this.stagingKeyCategoryMap,
                Object.keys(keyMap).length
                    ? { [category]: { $set: keyMap } }
                    : { $unset: [category] }
            )
            this.selectFirstItem()
        },
        addNewKey() {
            const plainKey = CREATE_TBL_TOKENS.key
            const currPlainKeyMap = this.stagingKeyCategoryMap[plainKey] || {}
            const newKey = {
                id: `key_${this.$helpers.uuidv1()}`,
                cols: [],
                name: `index_${Object.keys(currPlainKeyMap).length}`,
            }
            this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(this.stagingKeyCategoryMap, {
                $merge: {
                    [plainKey]: { ...currPlainKeyMap, [newKey.id]: newKey },
                },
            })
            // wait for the next tick to ensure the list is regenerated before selecting the item
            this.$nextTick(
                () => this.$refs.indexesList.handleSelectItem(-1) // select last item
            )
        },
    },
}
</script>
