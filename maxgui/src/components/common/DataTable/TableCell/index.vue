<template>
    <td
        :rowspan="cellIndex < colsHasRowSpan ? item.rowspan : null"
        :class="tdClasses(header, item, cellIndex)"
        :style="isTree && hasValidChild && cellLevelPadding(item, cellIndex)"
        @mouseenter="e => cellHover(e, item, rowIndex, cellIndex, header)"
        @mouseleave="e => cellHover(e, item, rowIndex, cellIndex, header)"
    >
        <v-icon
            v-if="draggable"
            v-show="showDragIcon(rowIndex, cellIndex)"
            :class="{ 'drag-handle move': draggable }"
            class="color text-field-text"
            size="16"
        >
            drag_handle
        </v-icon>

        <div
            ref="itemWrapperCell"
            :style="itemWrapperAlign(header)"
            :class="itemWrapperClasses(header, item, cellIndex)"
        >
            <!-- Display toggle button at the first column-->
            <v-btn
                v-if="cellIndex === 0 && item.children && item.children.length"
                width="32"
                height="32"
                class="arrow-toggle mr-1"
                icon
                @click="$emit('toggle-node', item)"
            >
                <v-icon
                    :class="[item.expanded === true ? 'arrow-up' : 'arrow-down']"
                    size="24"
                    color="#013646"
                >
                    $expand
                </v-icon>
            </v-btn>

            <!-- no content for the corresponding header, usually this is an error -->
            <span v-if="$help.isUndefined(item[header.value])"></span>
            <span
                v-else
                :id="`truncatedText_atRow${rowIndex}_atCell${cellIndex}_${componentId}`"
                ref="truncatedTextAtRow"
            >
                <slot :name="header.value" :data="{ item, header, cellIndex, rowIndex }" />
            </span>

            <!-- Actions slot -->
            <div v-if="renderActionsSlot(rowIndex, cellIndex)" class="action-slot-wrapper">
                <slot :data="{ item }" name="actions" />
            </div>
        </div>
    </td>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
SLOTS available for this component:
- slot :name="header.value" // slot aka item
- slot  name="actions" :data="{ item }"

Emits:
- $emit('get-truncated-info', truncatedMenu:Object)
- $emit('cell-hover', { e, item, rowIndex, cellIndex, header })
- $emit('toggle-node', item:Object)
*/
export default {
    name: 'table-cell',
    props: {
        cellIndex: { type: Number, required: true },
        item: { type: Object, required: true },
        header: { type: Object, required: true },
        indexOfLastColumn: { type: Number, required: true },
        rowIndex: { type: Number, required: true },
        hasOrderNumber: { type: Boolean, required: true },
        editableCell: { type: Boolean, required: true },
        tdBorderLeft: { type: Boolean, required: true },
        // For displaying draggable icon
        draggable: { type: Boolean, required: true },
        // for tree view
        isTree: { type: Boolean, required: true },
        hasValidChild: { type: Boolean, required: true },
        // for external v-menu activator
        componentId: { type: String, required: true },
        // For displaying action column slot
        indexOfHoveredRow: {
            type: Number,
        },
        // For displaying rowspan table
        colsHasRowSpan: { type: Number },
    },
    data() {
        return {
            //For truncated cell
            truncatedMenu: { index: null, x: 0, y: 16.5 },
        }
    },
    methods: {
        tdClasses(header, item, cellIndex) {
            return [
                // for showing index number columns from item
                this.hasOrderNumber &&
                    cellIndex === 0 &&
                    'overline px-2 color border-right-table-border text-field-text',
                // for rowspan feature
                item.hidden && cellIndex < this.colsHasRowSpan && 'hide',
                this.colsHasRowSpan &&
                    (cellIndex < this.colsHasRowSpan
                        ? `${item.groupId}-rowspan-cell`
                        : `${item.groupId}-cell`),
                // for editable feature
                this.editableCell && header.editableCol && 'v-data-table__editable-cell',
                // for action slots
                header.value === 'action' && 'pr-3',
                // for tree view feature
                item.expanded && 'font-weight-bold',
                // for styling and common class
                header.value,
                header.align && `text-${header.align}`,
                this.tdBorderLeft || cellIndex === this.colsHasRowSpan
                    ? 'color border-left-table-border'
                    : '',
                item.level > 0 || header.cellTruncated ? 'text-truncate cell-truncate' : '',
                this.draggable && 'relative',
            ]
        },

        itemWrapperClasses(header, item, cellIndex) {
            return [
                item.level > 0 || header.cellTruncated ? 'text-truncate' : '',
                'relative',
                (item.level > 0 || header.cellTruncated) &&
                    this.truncatedMenu.index === cellIndex &&
                    'pointer',
            ]
        },

        itemWrapperAlign(header) {
            // make centering cell more accurate by ignore the width of the sort arrow from the header
            let marginRight = header.align && header.sortable !== false ? 26 : ''
            return {
                marginRight: `${marginRight}px`,
            }
        },

        // Tree view
        cellLevelPadding(item, cellIndex) {
            const basePl = 8
            let levelPl = 30 * item.level
            !item.leaf ? (levelPl += 0) : (levelPl += 40)
            return `padding: 0px 48px 0px ${cellIndex === 0 ? basePl + levelPl : '48'}px`
        },

        // render actions slot at indexOfHoveredRow
        renderActionsSlot(rowIndex, cellIndex) {
            return (
                this.indexOfHoveredRow === rowIndex &&
                // show at last columns
                cellIndex === this.indexOfLastColumn
            )
        },

        // show drag handle icon at indexOfHoveredRow and show at last columns
        showDragIcon(rowIndex, cellIndex) {
            return this.indexOfHoveredRow === rowIndex && cellIndex === this.indexOfLastColumn
        },

        //---------------------------------Cell events----------------------------------------------------------------
        cellHover(e, item, rowIndex, cellIndex, header) {
            this.$emit('cell-hover', {
                e,
                item,
                rowIndex,
                cellIndex,
                header,
            })
            if (item.level > 0 || header.cellTruncated) {
                this.showTruncatedMenu(item, rowIndex, cellIndex, header)
            }
        },

        /**
         * @param {Object} item object
         * This function shows truncated text in v-menu
         */
        showTruncatedMenu(item, rowIndex, cellIndex, header) {
            // auto truncated text feature
            const wrapper = this.$refs.itemWrapperCell
            const text = this.$refs.truncatedTextAtRow

            if (wrapper && text && wrapper.offsetWidth < text.offsetWidth) {
                // const wrapperClientRect = wrapper.getBoundingClientRect()
                this.truncatedMenu.index = cellIndex
                this.truncatedMenu.x = text.offsetWidth - wrapper.offsetWidth
                this.truncatedMenu.rowIndex = rowIndex
                this.truncatedMenu.cellIndex = cellIndex
                this.truncatedMenu.item = item
                this.truncatedMenu.header = header

                this.$emit('get-truncated-info', this.truncatedMenu)
            } else {
                this.truncatedMenu.index = null
                this.$emit('get-truncated-info', this.truncatedMenu)
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.drag-handle {
    position: absolute;

    top: 10px;
    transform: translate(-50%, -50%);
}
.action-slot-wrapper {
    position: absolute;
    right: 0px;
    top: 50%;
    transform: translate(0%, -50%);
}
.cell-truncate {
    max-width: 1px;
}
</style>
