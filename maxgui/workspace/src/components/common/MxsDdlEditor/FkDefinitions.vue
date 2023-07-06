<template>
    <div class="fill-height">
        <!-- TODO: add toolbar to add/drop key -->
        <mxs-virtual-scroll-tbl
            :headers="headers"
            :rows="rows"
            :itemHeight="32"
            :maxHeight="dim.height"
            :boundingWidth="dim.width"
            showSelect
            :isVertTable="isVertTable"
            v-on="$listeners"
            @selected-rows="selectedItems = $event"
        >
        </mxs-virtual-scroll-tbl>
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

export default {
    name: 'fk-definitions',
    props: {
        value: { type: Array, required: true },
        initialData: { type: Array, required: true },
        dim: { type: Object, required: true },
    },
    data() {
        return {
            selectedItems: [],
            isVertTable: false,
        }
    },
    computed: {
        headers() {
            let header = { sortable: false }
            return [
                { text: 'id', hidden: true },
                { text: this.$mxs_t('name'), ...header },
                { text: this.$mxs_t('referencingCols'), minWidth: 146, ...header },
                { text: this.$mxs_t('referencedSchema'), width: 150, minWidth: 136, ...header },
                { text: this.$mxs_t('referencedTbl'), width: 150, minWidth: 124, ...header },
                { text: this.$mxs_t('referencedCols'), minWidth: 142, ...header },
                { text: this.$mxs_t('match'), width: 90, resizable: false, ...header },
                { text: this.$mxs_t('onUpdate'), width: 120, minWidth: 86, ...header },
                { text: this.$mxs_t('onDelete'), width: 120, minWidth: 86, ...header },
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
        rows() {
            //TODO: generate rows from keys
            return []
        },
    },
    methods: {},
}
</script>
