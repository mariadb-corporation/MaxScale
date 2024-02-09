<template>
    <div class="virtual-table__header d-flex relative">
        <div class="thead d-flex" :style="{ width: `${boundingWidth}px` }">
            <div
                v-if="!areHeadersHidden && showSelect && !isVertTable"
                class="th d-flex justify-center align-center"
                :style="{
                    ...headerStyle,
                    maxWidth: `${checkboxColWidth}px`,
                    minWidth: `${checkboxColWidth}px`,
                }"
            >
                <template v-if="!singleSelect">
                    <v-checkbox
                        :input-value="isAllSelected"
                        :indeterminate="indeterminate"
                        class="v-checkbox--mariadb-xs ma-0"
                        primary
                        hide-details
                        @change="val => $emit('toggle-select-all', val)"
                    />
                    <div class="header__resizer no-pointerEvent d-inline-block fill-height" />
                </template>
            </div>
            <template v-for="(header, index) in headers">
                <div
                    v-if="!header.hidden"
                    :id="headerIds[index]"
                    :key="headerIds[index]"
                    :ref="`header__${index}`"
                    :style="{
                        ...headerStyle,
                        maxWidth: $helpers.handleAddPxUnit(headerWidths[index]),
                        minWidth: $helpers.handleAddPxUnit(headerWidths[index]),
                    }"
                    class="th d-flex align-center px-3"
                    :class="{
                        pointer: enableSorting && header.sortable !== false,
                        [`sort--active ${sortOpts.sortDesc ? 'desc' : 'asc'}`]:
                            sortOpts.sortByColIdx === index,
                        'text-capitalize': header.capitalize,
                        'text-uppercase': header.uppercase,
                        'th--resizable': !isResizerDisabled(header),
                        'label-required': header.required,
                    }"
                    @click="isSortable(header) ? updateSortOpts(index) : null"
                >
                    <template v-if="index === 0 && header.text === '#'">
                        {{ header.text }}
                        <span v-if="showRowCount" class="ml-1 mxs-color-helper text-grayed-out">
                            ({{ rowCount }})
                        </span>
                    </template>
                    <slot
                        v-else
                        :name="`header-${header.text}`"
                        :data="{
                            header,
                            // maxWidth: minus padding and sort-icon
                            maxWidth: headerTxtMaxWidth({ header, index }),
                            colIdx: index,
                            activatorID: headerIds[index],
                        }"
                    >
                        <span class="text-truncate">{{ header.text }} </span>
                    </slot>
                    <v-icon v-if="isSortable(header)" size="14" class="sort-icon ml-2">
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                    <span
                        v-if="index < headers.length - 1"
                        class="header__resizer d-inline-block fill-height"
                        :data-test="`${header.text}-resizer-ele`"
                        v-on="
                            isResizerDisabled(header)
                                ? null
                                : { mousedown: e => resizerMouseDown(e, index) }
                        "
                    />
                </div>
            </template>
        </div>
        <div
            :style="{ minWidth: `${scrollBarThickness}px` }"
            class="d-inline-block fixed-padding"
        />
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 *
 items: {
  width?: string | number, default width when header is rendered
  maxWidth?: string | number, if maxWidth is declared, it ignores width and use it as default width
  minWidth?: string | number, allow resizing column to no smaller than provided value
  resizable?: boolean, true by default
  capitalize?: boolean, capitalize first letter of the header
  uppercase?: boolean, uppercase all letters of the header
  groupable?: boolean
  hidden?: boolean, hidden the column
  draggable?: boolean, emits on-cell-dragging and on-cell-dragend events when dragging the content of the cell
  sortable?: boolean, if false, column won't be sortable
  required?: boolean, if true, `label-required` class will be added to the header
  useCellSlot?: boolean, if true, v-mxs-highlighter directive and mouse events won't be added
  // attributes to be used with filtering
  valuePath?: string. When value of the cell is an object.
  dateFormatType?: string. date-fns format, E, dd MMM yyyy.
}
 */
export default {
    name: 'table-header',
    props: {
        items: { type: Array, required: true },
        boundingWidth: { type: Number, required: true },
        headerStyle: { type: Object, required: true },
        isVertTable: { type: Boolean, default: false },
        rowCount: { type: Number, required: true },
        showSelect: { type: Boolean, required: true },
        singleSelect: { type: Boolean, required: true },
        checkboxColWidth: { type: Number, required: true },
        isAllSelected: { type: Boolean, required: true },
        indeterminate: { type: Boolean, required: true },
        areHeadersHidden: { type: Boolean, required: true },
        scrollBarThickness: { type: Number, required: true },
        showRowCount: { type: Boolean, required: true },
        sortOptions: { type: Object, required: true }, // sync
    },
    data() {
        return {
            headerWidths: [],
            isResizing: false,
            resizingData: null,
            headerIds: [],
        }
    },
    computed: {
        headers() {
            return this.isVertTable
                ? [
                      { text: 'COLUMN', width: '20%' },
                      { text: 'VALUE', width: '80%' },
                  ]
                : this.items
        },
        visHeaders() {
            return this.headers.filter(h => !h.hidden)
        },
        lastVisHeader() {
            return this.visHeaders.at(-1)
        },
        sortOpts: {
            get() {
                return this.sortOptions
            },
            set(v) {
                this.$emit('update:sortOptions', v)
            },
        },
        enableSorting() {
            return this.rowCount <= 10000 && !this.isVertTable
        },

        //threshold, user cannot resize header smaller than this
        headerMinWidths() {
            return this.headers.map(h => h.minWidth || 67)
        },
    },
    watch: {
        isResizing(v) {
            this.$emit('is-resizing', v)
        },
        headers: {
            immediate: true,
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.headerIds = Array.from(
                        { length: this.headers.length },
                        () => `header-${this.$helpers.uuidv1()}`
                    )
                }
            },
        },
        headerIds: {
            immediate: true,
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.resetHeaderWidths()
                    this.$nextTick(() => this.computeHeaderWidths())
                }
            },
        },
        boundingWidth() {
            this.resetHeaderWidths()
            this.$nextTick(() => this.computeHeaderWidths())
        },
    },
    created() {
        window.addEventListener('mousemove', this.resizerMouseMove)
        window.addEventListener('mouseup', this.resizerMouseUp)
    },
    beforeDestroy() {
        window.removeEventListener('mousemove', this.resizerMouseMove)
        window.removeEventListener('mouseup', this.resizerMouseUp)
    },
    methods: {
        isSortable(header) {
            return this.enableSorting && header.sortable !== false
        },
        isResizerDisabled(header) {
            return header.text === '#' || header.resizable === false
        },
        headerTxtMaxWidth({ header, index }) {
            const w =
                this.$typy(this.headerWidths[index]).safeNumber -
                (this.isSortable(header) ? 22 : 0) -
                24 // padding
            return w > 0 ? w : 1
        },
        resetHeaderWidths() {
            this.headerWidths = this.headers.map(header =>
                header.hidden ? 0 : header.maxWidth || header.width || 'unset'
            )
        },
        getHeaderWidth(headerIdx) {
            const headerEle = this.$typy(this.$refs, `header__${headerIdx}[0]`).safeObject
            if (headerEle) {
                const { width } = headerEle.getBoundingClientRect()
                return width
            }
            return 0
        },
        computeHeaderWidths() {
            this.headerWidths = this.headers.map((header, index) => {
                const minWidth = this.headerMinWidths[index]
                const width = this.getHeaderWidth(index)
                return header.hidden ? 0 : width < minWidth ? minWidth : width
            })
            this.$emit('header-widths', this.headerWidths)
        },
        hasClass({ ele, className }) {
            let str = ` ${ele.className} `
            let classToFind = ` ${className} `
            return str.indexOf(classToFind) !== -1
        },
        getLastNode() {
            const index = this.headers.indexOf(this.lastVisHeader)
            return {
                node: this.$typy(this.$refs, `header__${index}[0]`).safeObject,
                lastNodeMinWidth: this.headerMinWidths[index],
            }
        },
        resizerMouseDown(e, index) {
            this.resizingData = {
                currPageX: e.pageX,
                targetNode: e.target.parentElement,
                targetNodeWidth: e.target.parentElement.offsetWidth,
                targetHeaderMinWidth: this.headerMinWidths[index],
            }
            this.isResizing = true
        },
        resizerMouseMove(e) {
            if (this.isResizing) {
                const { handleAddPxUnit } = this.$helpers
                const {
                    currPageX,
                    targetNode,
                    targetNodeWidth,
                    targetHeaderMinWidth,
                } = this.resizingData
                const diffX = e.pageX - currPageX
                const targetNewWidth = targetNodeWidth + diffX
                if (targetNewWidth >= targetHeaderMinWidth) {
                    targetNode.style.maxWidth = handleAddPxUnit(targetNewWidth)
                    targetNode.style.minWidth = handleAddPxUnit(targetNewWidth)
                    const { node: lastNode, lastNodeMinWidth } = this.getLastNode()
                    if (lastNode) {
                        // Let the last header node to auto grow its width
                        lastNode.style.maxWidth = 'unset'
                        lastNode.style.minWidth = 'unset'
                        // use the default min width if the actual width is smaller than it
                        const { width: currentLastNodeWidth } = lastNode.getBoundingClientRect()
                        if (currentLastNodeWidth < lastNodeMinWidth) {
                            lastNode.style.maxWidth = this.$helpers.handleAddPxUnit(
                                lastNodeMinWidth
                            )
                            lastNode.style.minWidth = this.$helpers.handleAddPxUnit(
                                lastNodeMinWidth
                            )
                        }
                    }
                }
            }
        },
        resizerMouseUp() {
            if (this.isResizing) {
                this.isResizing = false
                this.computeHeaderWidths()
                this.resizingData = null
            }
        },
        /**
         * Update sort options in three states: initial, asc or desc
         * @param {number} - index
         */
        updateSortOpts(index) {
            if (this.sortOpts.sortByColIdx === index) {
                if (this.sortOpts.sortDesc) this.sortOpts.sortByColIdx = -1
                this.sortOpts.sortDesc = !this.sortOpts.sortDesc
            } else {
                this.sortOpts.sortByColIdx = index
                this.sortOpts.sortDesc = false
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.virtual-table__header {
    height: 30px;
    overflow: hidden;
    .thead {
        .th {
            position: relative;
            z-index: 1;
            flex: 1;
            font-weight: bold;
            font-size: 0.75rem;
            color: $small-text;
            background-color: $table-border;
            border-bottom: none;
            user-select: none;
            height: 30px;
            &:first-child {
                border-radius: 5px 0 0 0;
            }
            &:last-child {
                padding-right: 0px !important;
            }
            .sort-icon {
                transform: none;
                visibility: hidden;
            }
            &.sort--active {
                color: black;
            }
            &.sort--active .sort-icon {
                color: inherit;
                visibility: visible;
            }
            &.desc {
                .sort-icon {
                    transform: rotate(-180deg);
                }
            }
            &:hover {
                .sort-icon {
                    visibility: visible;
                }
            }
            .header__resizer {
                position: absolute;
                right: 0px;
                width: 11px;
                border-right: 1px solid white;
                // disabled by default
                cursor: initial;
                &--hovered,
                &:hover {
                    border-right: 1px solid white;
                }
            }
            // Enable when have th--resizable class
            &--resizable {
                .header__resizer {
                    cursor: ew-resize;
                    border-right: 1px solid white;
                    &--hovered,
                    &:hover {
                        border-right: 3px solid white;
                    }
                }
            }
            &.label-required::after {
                left: 4px;
            }
        }
    }

    .fixed-padding {
        z-index: 2;
        background-color: $table-border;
        height: 30px;
        position: absolute;
        border-radius: 0 5px 0 0;
        right: 0;
    }
}
</style>
