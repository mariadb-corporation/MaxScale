<template>
    <tr
        ref="tableRow"
        :class="trClasses(rowIndex)"
        v-on="
            draggable || showActionsOnHover
                ? {
                      mouseenter: e => onRowHover(e, rowIndex),
                      mouseleave: e => onRowHover(e, rowIndex),
                  }
                : null
        "
    >
        <slot name="cell" :data="{ indexOfHoveredRow: indexOfHoveredRow }" />
    </tr>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
SLOTS available for this component:
- slot name="cell" :data="{  indexOfHoveredRow: indexOfHoveredRow }"

*/
export default {
    name: 'table-row',

    props: {
        rowIndex: { type: Number, required: true },
        editableCell: { type: Boolean, required: true },
        draggable: { type: Boolean, required: true },
        showActionsOnHover: { type: Boolean, required: true },
    },
    data() {
        return {
            // use when draggable or showActionsOnHover is enabled
            indexOfHoveredRow: null,
        }
    },

    methods: {
        trClasses() {
            return {
                // for editable feature
                'v-data-table__editable-cell-mode': this.editableCell,
                // for row draggble feature
                'draggable-row': this.draggable,
                'color bg-hovered-row__hovering': true,
            }
        },
        onRowHover(e, index) {
            const { type } = e
            switch (type) {
                case 'mouseenter':
                    {
                        this.indexOfHoveredRow = index
                        // positioning the drag handle to the center of the table row
                        if (this.draggable) {
                            let tableRowWidth = this.$refs.tableRow.clientWidth
                            let dragHandle = document.getElementsByClassName('drag-handle')
                            let center = `calc(100% - ${tableRowWidth / 2}px)`
                            if (dragHandle.length && dragHandle[0].style.left !== center)
                                for (const item of dragHandle) {
                                    item.style.left = center
                                }
                        }
                    }
                    break
                case 'mouseleave':
                    this.indexOfHoveredRow = null
                    break
            }
        },
    },
}
</script>
