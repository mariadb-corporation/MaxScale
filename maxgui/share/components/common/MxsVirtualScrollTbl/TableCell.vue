<template>
    <div
        :id="slotData.activatorID"
        class="td px-3"
        :class="{ [draggableClass]: isCellDraggable }"
        v-on="isCellDraggable ? { mousedown: e => $emit('mousedown', e) } : null"
        @contextmenu.prevent="
            $emit('on-cell-right-click', {
                e: $event,
                row: slotData.rowData,
                cell: slotData.cell,
                activatorID: slotData.activatorID,
            })
        "
    >
        <slot :name="slotName" :data="slotData">
            <mxs-truncate-str
                v-mxs-highlighter="{
                    keyword: $typy(slotData, 'search').safeString,
                    txt: slotData.cell,
                }"
                :disabled="slotData.isDragging"
                :tooltipItem="{ txt: `${slotData.cell}`, activatorID: slotData.activatorID }"
                :maxWidth="slotData.maxWidth"
            />
        </slot>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
    name: 'table-cell',
    props: {
        slotName: { type: String, required: true },
        slotData: { type: Object, required: true },
    },
    computed: {
        draggableClass() {
            return 'cursor--grab no-userSelect'
        },
        isCellDraggable() {
            return this.$typy(this.slotData, 'header.draggable').safeBoolean
        },
    },
}
</script>
