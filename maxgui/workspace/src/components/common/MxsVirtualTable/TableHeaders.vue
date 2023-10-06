<template>
    <div class="thead d-flex" :style="{ width: $helpers.handleAddPxUnit(boundingWidth) }">
        <div
            v-if="showOrderNumber"
            ref="orderNumberHeader"
            class="th order-number-header px-3 d-inline-flex align-center justify-center"
            :style="{
                width: 'max-content',
                borderRight: '1px solid white',
            }"
            data-test="order-number-header-ele"
        >
            #
            <span class="ml-1 mxs-color-helper text-grayed-out">({{ rowCount }})</span>
        </div>
        <template v-for="(header, index) in headers">
            <div
                v-if="!header.hidden"
                :id="headerIds[index]"
                :key="headerIds[index]"
                :ref="`header__${index}`"
                :style="{
                    maxWidth: $helpers.handleAddPxUnit(headerWidths[index]),
                    minWidth: $helpers.handleAddPxUnit(headerWidths[index]),
                }"
                class="th relative px-3 d-inline-flex align-center flex-grow-1"
                :class="{
                    'text-capitalize': header.capitalize,
                    'text-uppercase': header.uppercase,
                    'th--resizable': header.resizable,
                    pointer: enableSorting && header.sortable !== false,
                    [`sort--active ${sortOpts.sortDesc ? 'desc' : 'asc'}`]:
                        sortOpts.sortBy === header.text,
                }"
                :data-test="`${header.text}-ele`"
                @click="enableSorting && header.sortable !== false ? updateSortOpts(header) : null"
            >
                <slot
                    :name="`header-${header.text}`"
                    :data="{
                        header,
                        // maxWidth: minus padding and sort-icon
                        maxWidth: headerTxtMaxWidth(index),
                        index,
                        activatorID: headerIds[index],
                    }"
                >
                    <span
                        class="text-truncate"
                        :class="{ 'label-required': header.required }"
                        data-test="header-text-ele"
                    >
                        {{ header.text }}
                    </span>
                </slot>
                <v-icon
                    v-if="enableSorting && header.sortable !== false"
                    size="14"
                    class="sort-icon ml-2"
                >
                    $vuetify.icons.mxs_arrowDown
                </v-icon>
                <span
                    v-if="header.text !== $typy(lastVisHeader, 'text').safeString"
                    class="header__resizer d-inline-block fill-height"
                    :data-test="`${header.text}-resizer-ele`"
                    @click.stop
                    v-on="header.resizable ? { mousedown: e => resizerMouseDown(e, index) } : null"
                />
            </div>
        </template>
    </div>
</template>
<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 *
 headers: {
  text?: string, header text
  width?: string | number, default width when header is rendered
  maxWidth?: string | number, if maxWidth is declared, it ignores width and use it as default width
  minWidth?: string | number, allow resizing column to no smaller than provided value
  resizable?: boolean
  capitalize?: boolean, capitalize first letter of the header
  uppercase?: boolean, uppercase all letters of the header
  hidden?: boolean, hidden the column
  required?: boolean, if true, `label-required` class will be added to the header
  filter?: (value: any, search: string) => boolean, custom filter
  filterDateFormat?: string, date-fns format, E, dd MMM yyyy
  sortable?: boolean, true by default
}
 Emits:
 - is-resizing(boolean)
 - header-widths(array)
 - order-number-header-width(number)
 */
export default {
    name: 'table-headers',
    props: {
        headers: { type: Array, required: true },
        showOrderNumber: { type: Boolean, required: true },
        rowCount: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        sortOptions: { type: Object, required: true }, // sync
    },
    data() {
        return {
            headerWidths: [],
            orderNumberHeaderWidth: 0,
            isResizing: false,
            resizingData: null,
            headerIds: [],
        }
    },
    computed: {
        sortOpts: {
            get() {
                return this.sortOptions
            },
            set(v) {
                this.$emit('update:sortOptions', v)
            },
        },
        visHeaders() {
            return this.headers.filter(h => !h.hidden)
        },
        lastVisHeader() {
            return this.visHeaders.at(-1)
        },
        defaultHeaderMinWidth() {
            return 67
        },
        //threshold, user cannot resize header smaller than this
        headerMinWidths() {
            return this.headers.map(h => h.minWidth || this.defaultHeaderMinWidth)
        },
        enableSorting() {
            return this.rowCount <= 10000
        },
    },
    watch: {
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
        isResizing(v) {
            this.$emit('is-resizing', v)
        },
        rowCount: {
            immediate: true,
            handler() {
                this.$helpers.doubleRAF(() => this.computeOrderNumberHeaderWidth())
            },
        },
        headerIds: {
            immediate: true,
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.resetHeaderWidths()
                    this.$helpers.doubleRAF(() => {
                        if (this.showOrderNumber) this.computeOrderNumberHeaderWidth()
                        this.computeHeaderWidths()
                    })
                }
            },
        },
        boundingWidth() {
            this.resetHeaderWidths()
            this.$helpers.doubleRAF(() => this.computeHeaderWidths())
        },
    },
    mounted() {
        window.addEventListener('mousemove', this.resizerMouseMove)
        window.addEventListener('mouseup', this.resizerMouseUp)
    },
    beforeDestroy() {
        window.removeEventListener('mousemove', this.resizerMouseMove)
        window.removeEventListener('mouseup', this.resizerMouseUp)
    },
    methods: {
        areNumbers(arr) {
            return arr.every(v => this.$typy(v).isNumber)
        },
        headerTxtMaxWidth(index) {
            const w = this.$typy(this.headerWidths[index]).safeNumber - 24 // padding
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
        computeOrderNumberHeaderWidth() {
            const ele = this.$typy(this.$refs, 'orderNumberHeader').safeObject
            if (ele) this.$emit('order-number-header-width', ele.getBoundingClientRect().width)
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
         * @param {object} - header
         */
        updateSortOpts(header) {
            if (this.sortOpts.sortBy === header.text) {
                if (this.sortOpts.sortDesc) this.sortOpts.sortBy = -1
                this.sortOpts.sortDesc = !this.sortOpts.sortDesc
            } else {
                this.sortOpts.sortBy = header.text
                this.sortOpts.sortDesc = false
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.thead {
    .th {
        font-weight: bold;
        font-size: 0.75rem;
        color: $small-text;
        background-color: $table-border;
        user-select: none;
        &:first-child {
            border-radius: 5px 0 0 0;
        }
        &:last-child:not(.order-number-header) {
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
        // disabled by default
        .header__resizer {
            position: absolute;
            right: 0px;
            width: 11px;
            border-right: 1px solid white;
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
</style>
