<template>
    <!-- Use searchHighlighterDisabled as key to rerender the cell
    to make sure the slot is rendered with accurate slot content,
    otherwise, it renders the old content created by the highlighter directive
    -->
    <div
        :id="slotData.activatorID"
        ref="cell"
        :key="slotData.header.searchHighlighterDisabled"
        v-mxs-highlighter="slotData.header.searchHighlighterDisabled ? null : highlighterData"
        class="td px-3 d-inline-block text-truncate"
        :class="{
            [draggableClass]: isCellDraggable,
        }"
        @mouseenter="slotData.isDragging ? null : mouseenterHandler($event)"
        @mousedown="isCellDraggable ? $emit('mousedown', $event) : null"
        @contextmenu.prevent="
            $emit('on-cell-right-click', {
                e: $event,
                row: slotData.rowData,
                cell: slotData.cell,
                activatorID: slotData.activatorID,
            })
        "
    >
        <slot :name="slotName" :data="{ ...slotData, highlighterData }">{{ slotData.cell }} </slot>
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'
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
        highlighterData() {
            return {
                keyword: this.$typy(this.slotData, 'search').safeString,
                txt: this.slotData.cell,
            }
        },
    },
    created() {
        this.debouncedShowTooltip = this.$helpers.lodash.debounce(() => {
            this.SET_TRUNCATE_TOOLTIP_ITEM(
                this.$typy(this.$refs, 'cell.scrollWidth').safeNumber >
                    this.$typy(this.$refs, 'cell.clientWidth').safeNumber
                    ? { txt: this.slotData.cell, activatorID: this.slotData.activatorID }
                    : null
            )
        }, 300)
    },
    methods: {
        ...mapMutations({ SET_TRUNCATE_TOOLTIP_ITEM: 'mxsApp/SET_TRUNCATE_TOOLTIP_ITEM' }),
        mouseenterHandler() {
            this.debouncedShowTooltip()
        },
    },
}
</script>
