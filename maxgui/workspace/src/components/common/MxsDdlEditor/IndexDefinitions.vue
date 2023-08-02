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
        <indexes-list
            v-model="stagingKeys"
            :dim="{ height: dim.height - headerHeight, width: keyTblWidth }"
            :selectedItems.sync="selectedItems"
        />
        <!-- TODO: based on selectedItems, show a component to alter column order
        index order .i.e ASC or DESC, length.
         -->
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

export default {
    name: 'index-definitions',
    components: { TblToolbar, IndexesList },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        tablesColNameMap: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItems: [],
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
            return this.dim.width - this.keyTblWidth
        },
        keys: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
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
        deleteSelectedKeys(selectedItems) {
            // The index table has singleSelect, so selectedItems will have just 1 item
            const item = selectedItems[0]
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
