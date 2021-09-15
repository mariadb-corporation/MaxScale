<template>
    <td
        :rowspan="cellIndex < colsHasRowSpan ? item.rowspan : null"
        :class="tdClasses"
        :style="isTree && hasValidChild && cellLevelPadding"
        @mouseenter="e => cellHover(e)"
        @mouseleave="e => cellHover(e)"
    >
        <v-icon
            v-if="draggable"
            v-show="showDragIcon"
            :class="{ 'drag-handle move': draggable }"
            class="color text-field-text"
            size="16"
        >
            drag_handle
        </v-icon>

        <div
            :id="`truncatedText_atRow${rowIndex}_atCell${cellIndex}_${componentId}`"
            ref="itemWrapperCell"
            :style="{ ...itemWrapperAlign, lineHeight }"
            :class="itemWrapperClasses"
        >
            <!-- Display toggle button at the first column-->
            <v-btn
                v-if="shouldShowToggleBtn"
                width="32"
                height="32"
                class="arrow-toggle mr-1"
                icon
                @click="$emit('toggle-node', item)"
            >
                <v-icon
                    :class="[item.expanded === true ? 'arrow-down' : 'arrow-right']"
                    size="24"
                    color="deep-ocean"
                >
                    $expand
                </v-icon>
            </v-btn>

            <!-- no content for the corresponding header, usually this is an error -->
            <span v-if="$help.isUndefined(item[header.value])"></span>
            <div
                v-else-if="!editableCell || header.cellTruncated"
                ref="truncatedTextAtRow"
                class="d-inline"
            >
                <slot :name="header.value" :data="{ item, header, cellIndex, rowIndex }" />
            </div>
            <slot v-else :name="header.value" :data="{ item, header, cellIndex, rowIndex }" />

            <!-- Actions slot -->
            <div v-if="renderActionsSlot" class="action-slot-wrapper">
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
 * Change Date: 2025-08-17
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
            truncatedMenu: { index: null, x: 0 },
        }
    },
    computed: {
        shouldShowToggleBtn() {
            return Boolean(this.cellIndex === 0 && this.item.children && this.item.children.length)
        },
        itemWrapperClasses() {
            return [
                this.item.level > 0 || this.header.cellTruncated ? 'text-truncate' : '',
                'relative',
                (this.item.level > 0 || this.header.cellTruncated) &&
                    this.truncatedMenu.index === this.cellIndex &&
                    'pointer',
            ]
        },
        lineHeight: () => '44px',
        tdClasses() {
            return [
                'color border-bottom-table-border text-navigation',
                // for showing index number columns from item
                this.hasOrderNumber &&
                    this.cellIndex === 0 &&
                    'text-overline px-2 border-right-table-border text-field-text',
                // for rowspan feature
                this.item.hidden && this.cellIndex < this.colsHasRowSpan && 'hide',
                this.colsHasRowSpan &&
                    (this.cellIndex < this.colsHasRowSpan
                        ? `${this.item.groupId}-rowspan-cell`
                        : `${this.item.groupId}-cell`),
                // for editable feature
                this.editableCell && this.header.editableCol && 'v-data-table__editable-cell',
                // for action slots
                this.header.value === 'action' && 'pr-3',
                // for tree view feature
                this.item.expanded && 'font-weight-bold',
                // for styling and common class
                this.header.value,
                this.header.align && `text-${this.header.align}`,
                this.tdBorderLeft || this.cellIndex === this.colsHasRowSpan
                    ? 'border-left-table-border'
                    : '',
                this.item.level > 0 || this.header.cellTruncated
                    ? 'text-truncate cell-truncate'
                    : '',
                this.draggable && 'relative',
                `cell-${this.cellIndex}-${this.item.id}`,
            ]
        },
        itemWrapperAlign() {
            // make centering cell more accurate by ignore the width of the sort arrow from the header
            let marginRight = this.header.align && this.header.sortable !== false ? 26 : ''
            return {
                marginRight: `${marginRight}px`,
            }
        },
        // Tree view
        cellLevelPadding() {
            const basePl = 8
            let levelPl = 30 * this.item.level
            !this.item.leaf ? (levelPl += 0) : (levelPl += 40)
            return `padding: 0px 48px 0px ${this.cellIndex === 0 ? basePl + levelPl : '48'}px`
        },
        // render actions slot at indexOfHoveredRow
        renderActionsSlot() {
            return (
                this.indexOfHoveredRow === this.rowIndex &&
                // show at last columns
                this.cellIndex === this.indexOfLastColumn
            )
        },
        // show drag handle icon at indexOfHoveredRow and show at last columns
        showDragIcon() {
            return (
                this.indexOfHoveredRow === this.rowIndex &&
                this.cellIndex === this.indexOfLastColumn
            )
        },
    },
    methods: {
        //---------------------------------Cell events----------------------------------------------------------------
        cellHover(e) {
            const { item, rowIndex, cellIndex, header } = this
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
         * This function shows truncated text in v-menu
         * @param {Object} item object
         */
        showTruncatedMenu(item, rowIndex, cellIndex, header) {
            // auto truncated text feature
            const wrapper = this.$refs.itemWrapperCell
            const text = this.$refs.truncatedTextAtRow
            let wrapperOffsetWidth = this.$typy(wrapper, 'offsetWidth').safeNumber
            // Subtracting toggle button's width from wrapper's width
            if (this.shouldShowToggleBtn) wrapperOffsetWidth = wrapperOffsetWidth - 36
            const txtOffsetWidth = this.$typy(text, 'offsetWidth').safeNumber
            if (wrapperOffsetWidth < txtOffsetWidth) {
                this.truncatedMenu.index = cellIndex
                this.truncatedMenu.x = (txtOffsetWidth - wrapperOffsetWidth + 16) / 2
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
