<template>
    <v-text-field
        :id="targetCell.id"
        v-model.trim="targetCell.value"
        :name="targetCell.id"
        class="vuetify-input--override fill-height align-center"
        single-line
        outlined
        dense
        :height="26"
        autocomplete="off"
        hide-details
        @input="onInput"
    />
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
export default {
    name: 'editable-cell',
    props: {
        cellItem: { type: Object, required: true }, // must have id and value attrs
        changedCells: { type: Array, required: true }, // sync
    },
    data() {
        return { targetCell: {} }
    },
    computed: {
        //Detect if the cell value has been modified
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.targetCell, this.cellItem)
        },
    },
    watch: {
        cellItem: {
            deep: true,
            immediate: true,
            handler(n, o) {
                if (!this.$helpers.lodash.isEqual(n, o))
                    this.targetCell = this.$helpers.lodash.cloneDeep(n)
            },
        },
    },
    methods: {
        onInput() {
            let changedCells = this.$helpers.lodash.cloneDeep(this.changedCells)
            const targetIndex = changedCells.findIndex(c => c.id === this.targetCell.id)
            if (this.hasChanged) {
                // if this.targetCell is not included in changedCells
                if (targetIndex === -1) changedCells.push(this.targetCell)
                // if this.targetCell is already included in changedCells, but its value is changed again
                else changedCells[targetIndex] = this.targetCell
                this.$emit('update:changedCells', changedCells)
            } else if (targetIndex > -1) {
                // remove item from changedCells at targetIndex
                changedCells.splice(targetIndex, 1)
                this.$emit('update:changedCells', changedCells)
            }
        },
    },
}
</script>
