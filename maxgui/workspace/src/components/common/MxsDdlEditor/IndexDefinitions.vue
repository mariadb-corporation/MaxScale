<template>
    <div class="fill-height">
        <tbl-toolbar
            :selectedItems="[selectedItem]"
            :showRotateTable="false"
            reverse
            @get-computed-height="headerHeight = $event"
            @on-delete-selected-items="deleteSelectedKeys"
            @on-add="addNewKey"
        />
        <div class="d-flex flex-row">
            <indexes-list
                v-model="stagingKeys"
                :dim="{ height: dim.height - headerHeight, width: keyTblWidth }"
                :selectedItem.sync="selectedItem"
                class="mr-4"
            />
            <index-cols-list
                v-if="selectedKeyId"
                v-model="stagingKeys"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import TblToolbar from '@wsSrc/components/common/MxsDdlEditor/TblToolbar.vue'
import IndexesList from '@wsSrc/components/common/MxsDdlEditor/IndexesList.vue'
import IndexColsList from '@wsSrc/components/common/MxsDdlEditor/IndexColsList.vue'

export default {
    name: 'index-definitions',
    components: { TblToolbar, IndexesList, IndexColsList },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        tableColNameMap: { type: Object, required: true },
        tableColMap: { type: Array, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItem: [],
            stagingKeys: {},
        }
    },
    computed: {
        ...mapState({
            KEY_EDITOR_ATTRS: state => state.mxsWorkspace.config.KEY_EDITOR_ATTRS,
            KEY_EDITOR_ATTR_IDX_MAP: state => state.mxsWorkspace.config.KEY_EDITOR_ATTR_IDX_MAP,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
        }),
        idxOfId() {
            return this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.ID]
        },
        idxOfCategory() {
            return this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.CATEGORY]
        },
        keyTblWidth() {
            return this.dim.width / 2
        },
        keyColsTblWidth() {
            return this.dim.width - this.keyTblWidth - 16
        },
        keys: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        selectedKeyId() {
            const idx = this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.ID]
            return this.selectedItem[idx]
        },
        selectedKeyCategory() {
            const idx = this.KEY_EDITOR_ATTR_IDX_MAP[this.KEY_EDITOR_ATTRS.CATEGORY]
            return this.selectedItem[idx]
        },
    },
    watch: {
        keys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingKeys)) this.assignData()
            },
        },
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
        },
        deleteSelectedKeys() {
            const item = this.selectedItem
            const id = item[this.idxOfId]
            const category = item[this.idxOfCategory]
            let keyMap = this.stagingKeys[category] || {}
            keyMap = this.$helpers.immutableUpdate(keyMap, {
                $unset: [id],
            })
            this.stagingKeys = this.$helpers.immutableUpdate(
                this.stagingKeys,
                Object.keys(keyMap).length
                    ? { $merge: { [category]: keyMap } }
                    : { $unset: [category] }
            )
        },
        addNewKey() {
            const plainKey = this.CREATE_TBL_TOKENS.key
            const currPlainKeyMap = this.stagingKeys[plainKey] || {}
            const newKey = {
                id: `key_${this.$helpers.uuidv1()}`,
                cols: [],
                name: `index_${Object.keys(currPlainKeyMap).length}`,
            }
            this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                $merge: {
                    [plainKey]: { ...currPlainKeyMap, [newKey.id]: newKey },
                },
            })
        },
    },
}
</script>
