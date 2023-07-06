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
                <v-checkbox
                    :input-value="isAllselected"
                    :indeterminate="indeterminate"
                    dense
                    class="v-checkbox--mariadb-xs ma-0"
                    primary
                    hide-details
                    @change="val => $emit('toggle-select-all', val)"
                />
                <div class="header__resizer no-pointerEvent d-inline-block fill-height"></div>
            </div>
            <template v-for="(header, colIdx) in tableHeaders">
                <div
                    v-if="!header.hidden"
                    :id="genHeaderColID(colIdx)"
                    :key="`${header.text}_${colIdx}`"
                    :ref="`header__${colIdx}`"
                    :style="{
                        ...headerStyle,
                        maxWidth: $helpers.handleAddPxUnit(headerWidthMap[colIdx]),
                        minWidth: $helpers.handleAddPxUnit(headerWidthMap[colIdx]),
                    }"
                    class="th d-flex align-center px-3"
                    :class="{
                        pointer: enableSorting && header.sortable !== false,
                        [`sort--active ${sortOrder}`]: activeSort === header.text,
                        'text-capitalize': header.capitalize,
                        'th--resizable': !isResizerDisabled(header),
                    }"
                    @click="
                        () =>
                            enableSorting && header.sortable !== false
                                ? handleSort(header.text)
                                : null
                    "
                >
                    <template v-if="header.text === '#'">
                        <span> {{ header.text }}</span>
                        <span class="ml-1 mxs-color-helper text-grayed-out">
                            ({{ curr2dRowsLength }})
                        </span>
                    </template>
                    <slot
                        v-else
                        :name="`header-${header.text}`"
                        :data="{
                            header,
                            // maxWidth: minus padding and sort-icon
                            maxWidth: headerTxtMaxWidth({ header, colIdx }),
                            colIdx: colIdx,
                            activatorID: genHeaderColID(colIdx),
                        }"
                    >
                        {{ header.text }}
                    </slot>

                    <v-icon
                        v-if="enableSorting && header.sortable !== false"
                        size="14"
                        class="sort-icon ml-2"
                    >
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                    <span
                        v-if="enableGrouping && $typy(header, 'groupable').safeBoolean"
                        class="ml-2 text-none"
                        :class="[
                            activeGroupBy === header.text && !isVertTable
                                ? 'group--active'
                                : 'group--inactive',
                        ]"
                        @click.stop="() => handleToggleGroup(header.text)"
                    >
                        {{ $mxs_t('group') }}
                    </span>
                    <div
                        v-if="header.text !== $typy(lastVisHeader, 'text').safeString"
                        class="header__resizer d-inline-block fill-height"
                        v-on="
                            isResizerDisabled(header)
                                ? null
                                : { mousedown: e => resizerMouseDown(e, colIdx) }
                        "
                    />
                </div>
            </template>
        </div>
        <div
            :style="{ minWidth: `${scrollBarThicknessOffset}px` }"
            class="d-inline-block dummy-header"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 *
 headers: {
  width?: string | number, default width when header is rendered
  maxWidth?: string | number, if maxWidth is declared, it ignores width and use it as default width
  minWidth?: string | number, allow resizing column to no smaller than provided value
  resizable?: boolean, true by default
  capitalize?: boolean, capitalize first letter of the header
  groupable?: boolean
  customGroup?: (data:object) => rowMap. data.rows(2d array to be grouped). data.idx(col index of the inner array)
  hidden?: boolean, hidden the column
  draggable?: boolean, emits on-cell-dragging and on-cell-dragend events when dragging the content of the cell
  sortable?: boolean, if false, column won't be sortable
}
 */
export default {
    name: 'table-header',
    props: {
        headers: { type: Array, required: true },
        boundingWidth: { type: Number, required: true },
        headerStyle: { type: Object, required: true },
        isVertTable: { type: Boolean, default: false },
        curr2dRowsLength: { type: Number, required: true },
        showSelect: { type: Boolean, required: true },
        checkboxColWidth: { type: Number, required: true },
        isAllselected: { type: Boolean, required: true },
        indeterminate: { type: Boolean, required: true },
        areHeadersHidden: { type: Boolean, required: true },
        scrollBarThicknessOffset: { type: Number, required: true },
    },
    data() {
        return {
            headerWidthMap: {},
            isResizing: false,
            resizingData: {
                currCol: null,
                nxtCol: null,
                currPageX: 0,
                nxtColWidth: 0,
                currColWidth: 0,
                currColIndex: 0,
                nxtColIndex: 0,
            },
            sortOrder: 'asc',
            activeSort: '',
            activeGroupBy: '',
        }
    },
    computed: {
        tableHeaders() {
            return this.isVertTable
                ? [
                      { text: 'COLUMN', width: '20%' },
                      { text: 'VALUE', width: '80%' },
                  ]
                : this.headers
        },
        visHeaders() {
            return this.tableHeaders.filter(h => !h.hidden)
        },
        lastVisHeader() {
            if (this.visHeaders.length) return this.visHeaders.at(-1)
            return {}
        },
        enableSorting() {
            return this.curr2dRowsLength <= 10000 && !this.isVertTable
        },
        enableGrouping() {
            return this.tableHeaders.filter(h => !h.hidden).length > 1
        },
        //threshold, user cannot resize header smaller than this
        headerMinWidthMap() {
            return this.tableHeaders.reduce((obj, h) => {
                obj[h.text] = h.minWidth
                    ? h.minWidth
                    : this.$typy(h, 'groupable').safeBoolean
                    ? 117
                    : 67
                return obj
            }, {})
        },
    },
    watch: {
        tableHeaders: {
            deep: true,
            immediate: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.getComputedWidth()
            },
        },
        boundingWidth() {
            this.getComputedWidth()
        },
        headerWidthMap: {
            deep: true,
            handler(v) {
                this.$emit('get-header-width-map', v)
            },
        },
        isResizing(v) {
            this.$emit('is-resizing', v)
        },
    },
    created() {
        window.addEventListener('mousemove', this.resizerMouseMove)
        window.addEventListener('mouseup', this.resizerMouseUp)
    },
    destroyed() {
        window.removeEventListener('mousemove', this.resizerMouseMove)
        window.removeEventListener('mouseup', this.resizerMouseUp)
    },
    methods: {
        genHeaderColID(colIdx) {
            return `table-header__header-text_${colIdx}`
        },
        headerTxtMaxWidth({ header, colIdx }) {
            const w =
                this.$typy(this.headerWidthMap[colIdx]).safeNumber -
                (this.enableSorting && header.sortable !== false ? 22 : 0) -
                (this.enableGrouping && this.$typy(header, 'groupable').safeBoolean ? 38 : 0) -
                24 // padding
            return w > 0 ? w : 1
        },
        isResizerDisabled(header) {
            return header.text === '#' || header.resizable === false
        },
        resetHeaderWidth() {
            let headerWidthMap = {}
            for (const [colIdx, header] of this.tableHeaders.entries()) {
                headerWidthMap = {
                    ...headerWidthMap,
                    [colIdx]: header.maxWidth
                        ? header.maxWidth
                        : header.width
                        ? header.width
                        : 'unset',
                }
            }
            this.headerWidthMap = headerWidthMap
        },
        getHeaderWidth(headerIdx) {
            const headerEle = this.$typy(this.$refs, `header__${headerIdx}[0]`).safeObject
            if (headerEle) {
                const { width } = headerEle.getBoundingClientRect()
                return width
            }
            return 0
        },
        assignHeaderWidthMap() {
            let headerWidthMap = {}
            // get width of each header then use it to set same width of corresponding cells
            for (const [colIdx, header] of this.tableHeaders.entries()) {
                const minWidth = this.headerMinWidthMap[header.text]
                const width = this.getHeaderWidth(colIdx)
                const headerWidth = width < minWidth ? minWidth : width
                headerWidthMap = { ...headerWidthMap, [colIdx]: headerWidth }
            }
            this.headerWidthMap = headerWidthMap
        },
        getComputedWidth() {
            this.resetHeaderWidth()
            this.$nextTick(() => this.assignHeaderWidthMap())
        },
        hasClass({ ele, className }) {
            let str = ` ${ele.className} `
            let classToFind = ` ${className} `
            return str.indexOf(classToFind) !== -1
        },
        getNxtColByClass({ node, className, nxtColIndex }) {
            while ((node = node.nextSibling)) {
                nxtColIndex++
                if (this.hasClass({ ele: node, className })) return { node, nxtColIndex }
            }
            return { node: null, nxtColIndex }
        },
        resizerMouseDown(e, colIdx) {
            let resizingData = {
                currColIndex: colIdx,
                currCol: e.target.parentElement,
                currPageX: e.pageX,
                currColWidth: e.target.parentElement.offsetWidth,
            }
            const { node: nxtCol, nxtColIndex } = this.getNxtColByClass({
                node: resizingData.currCol,
                className: 'th--resizable',
                nxtColIndex: colIdx,
            })
            resizingData = { ...resizingData, nxtCol, nxtColIndex }
            if (resizingData.nxtCol) resizingData.nxtColWidth = resizingData.nxtCol.offsetWidth
            this.isResizing = true
            this.resizingData = { ...this.resizingData, ...resizingData }
        },
        resizerMouseMove(e) {
            if (this.isResizing) {
                const {
                    currPageX,
                    currCol,
                    currColWidth,
                    currColIndex,
                    nxtCol,
                    nxtColWidth,
                    nxtColIndex,
                } = this.resizingData
                const diffX = e.pageX - currPageX
                if (
                    currColWidth + diffX >=
                    this.headerMinWidthMap[this.tableHeaders[currColIndex].text]
                ) {
                    const newCurrColW = `${currColWidth + diffX}px`
                    currCol.style.maxWidth = newCurrColW
                    currCol.style.minWidth = newCurrColW
                    if (
                        nxtCol &&
                        nxtColWidth - diffX >=
                            this.headerMinWidthMap[this.tableHeaders[nxtColIndex].text]
                    ) {
                        const newNxtColW = `${nxtColWidth - diffX}px`
                        nxtCol.style.maxWidth == newNxtColW
                        nxtCol.style.minWidth = newNxtColW
                    }
                    this.$nextTick(() => this.assignHeaderWidthMap())
                }
            }
        },
        resizerMouseUp() {
            if (this.isResizing) {
                this.isResizing = false
                this.resizingData = {}
            }
        },
        /**
         * @param {String} h - header name
         */
        handleSort(h) {
            if (this.activeSort === h)
                switch (this.sortOrder) {
                    case 'asc':
                        this.sortOrder = 'desc'
                        break
                    case 'desc':
                        this.sortOrder = 'asc'
                        break
                }
            else {
                this.activeSort = h
                this.sortOrder = 'asc'
            }
            this.$emit('on-sorting', {
                sortBy: this.activeSort,
                isDesc: this.sortOrder !== 'asc',
            })
        },
        /**
         * @param {String} header - header name
         */
        handleToggleGroup(headerName) {
            if (this.activeGroupBy === headerName) this.activeGroupBy = ''
            else this.activeGroupBy = headerName
            this.$emit('on-group', this.activeGroupBy)
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
            .group--inactive {
                color: $small-text !important;
                opacity: 0.6;
                &:hover {
                    opacity: 1;
                }
            }
            .group--active {
                color: black;
                opacity: 1;
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
        }
    }

    .dummy-header {
        z-index: 2;
        background-color: $table-border;
        height: 30px;
        position: absolute;
        border-radius: 0 5px 0 0;
        right: 0;
    }
}
</style>
