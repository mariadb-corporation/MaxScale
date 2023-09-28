<template>
    <div class="thead d-flex" :style="{ width: $helpers.handleAddPxUnit(boundingWidth) }">
        <div
            v-if="showOrderNumber"
            ref="orderNumberHeader"
            class="th px-3 d-inline-flex align-center justify-center"
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
                    maxWidth: $helpers.handleAddPxUnit(headerWidthMap[index]),
                    minWidth: $helpers.handleAddPxUnit(headerWidthMap[index]),
                }"
                class="th relative px-3 d-inline-flex align-center flex-grow-1"
                :class="{
                    'text-capitalize': header.capitalize,
                    'text-uppercase': header.uppercase,
                    'th--resizable': header.resizable,
                }"
                :data-test="`${header.text}-ele`"
            >
                <span
                    class="text-truncate"
                    :class="{ 'label-required': header.required }"
                    data-test="header-text-ele"
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
                        {{ header.text }}
                    </slot>
                </span>
                <span
                    v-if="header.text !== $typy(lastVisHeader, 'text').safeString"
                    class="header__resizer d-inline-block fill-height"
                    :data-test="`${header.text}-resizer-ele`"
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
}
 Emits:
 - is-resizing(boolean)
 - header-width-map(object)
 - order-number-header-width(number)
 */
export default {
    name: 'table-headers',
    props: {
        headers: { type: Array, required: true },
        showOrderNumber: { type: Boolean, required: true },
        rowCount: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
    },
    data() {
        return {
            headerWidthMap: {},
            isResizing: false,
            resizingData: null,
            isWatched: false,
        }
    },
    computed: {
        visHeaders() {
            return this.headers.filter(h => !h.hidden)
        },
        lastVisHeader() {
            return this.visHeaders.at(-1)
        },
        headerIds() {
            return Array.from({ length: this.headers.length }, () => this.$helpers.uuidv1())
        },
        defaultHeaderMinWidth() {
            return 67
        },
        //threshold, user cannot resize header smaller than this
        headerMinWidths() {
            return this.headers.map(h => h.minWidth || this.defaultHeaderMinWidth)
        },
    },
    watch: {
        isResizing(v) {
            this.$emit('is-resizing', v)
        },
    },
    mounted() {
        window.addEventListener('mousemove', this.resizerMouseMove)
        window.addEventListener('mouseup', this.resizerMouseUp)
        this.addWatchers()
    },
    beforeDestroy() {
        window.removeEventListener('mousemove', this.resizerMouseMove)
        window.removeEventListener('mouseup', this.resizerMouseUp)
        this.rmWatchers()
    },
    activated() {
        this.addWatchers()
    },
    deactivated() {
        this.rmWatchers()
    },
    methods: {
        addWatchers() {
            if (!this.isWatched) {
                this.isWatched = true
                this.watch_headerIds()
                this.watch_boundingWidth()
                this.watch_headerWidthMap()
            }
        },
        rmWatchers() {
            this.$typy(this.unwatch_headerIds).safeFunction()
            this.$typy(this.unwatch_boundingWidth).safeFunction()
            this.$typy(this.unwatch_headerWidthMap).safeFunction()
        },
        watch_headerIds() {
            this.unwatch_headerIds = this.$watch(
                'headerIds',
                (v, oV) => {
                    if (!this.$helpers.lodash.isEqual(v, oV)) {
                        this.resetHeaderWidth()
                        this.$helpers.doubleRAF(() => {
                            if (this.showOrderNumber) this.computeOrderNumberHeaderWidth()
                            this.computeHeaderWidthMap()
                        })
                    }
                },
                { deep: true, immediate: true }
            )
        },
        watch_boundingWidth() {
            this.unwatch_boundingWidth = this.$watch('boundingWidth', () => {
                this.resetHeaderWidth()
                this.$helpers.doubleRAF(() => this.computeHeaderWidthMap())
            })
        },
        watch_headerWidthMap() {
            this.unwatch_headerWidthMap = this.$watch(
                'headerWidthMap',
                (v, oV) => {
                    if (
                        !this.$typy(v).isEmptyObject &&
                        !this.$helpers.lodash.isEqual(v, oV) &&
                        this.areNumbers(Object.values(this.headerWidthMap))
                    )
                        this.$emit('header-width-map', v)
                },
                { deep: true }
            )
        },
        areNumbers(arr) {
            return arr.every(v => this.$typy(v).isNumber)
        },
        headerTxtMaxWidth(index) {
            const w = this.$typy(this.headerWidthMap[index]).safeNumber - 24 // padding
            return w > 0 ? w : 1
        },
        resetHeaderWidth() {
            let headerWidthMap = {}
            for (const [index, header] of this.headers.entries()) {
                headerWidthMap = {
                    ...headerWidthMap,
                    [index]: header.maxWidth || header.width || 'unset',
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
        computeOrderNumberHeaderWidth() {
            const ele = this.$typy(this.$refs, 'orderNumberHeader').safeObject
            if (ele) this.$emit('order-number-header-width', ele.getBoundingClientRect().width)
        },
        computeHeaderWidthMap() {
            let headerWidthMap = {}
            this.headers.forEach((_, index) => {
                const minWidth = this.headerMinWidths[index]
                const width = this.getHeaderWidth(index)
                const headerWidth = width < minWidth ? minWidth : width
                headerWidthMap = { ...headerWidthMap, [index]: headerWidth }
            })
            this.headerWidthMap = headerWidthMap
        },
        hasClass({ ele, className }) {
            let str = ` ${ele.className} `
            let classToFind = ` ${className} `
            return str.indexOf(classToFind) !== -1
        },
        getNxtResizableNodeByClass({ node, className }) {
            while ((node = node.nextSibling)) {
                if (this.hasClass({ ele: node, className })) return node
            }
            return null
        },
        resizerMouseDown(e, index) {
            const nxtNode = this.getNxtResizableNodeByClass({
                node: e.target.parentElement,
                className: 'th--resizable',
            })
            this.resizingData = {
                currPageX: e.pageX,
                // target metadata
                targetNode: e.target.parentElement,
                targetNodeWidth: e.target.parentElement.offsetWidth,
                targetHeaderMinWidth: this.headerMinWidths[index],
                // next resizable header node
                nxtNode,
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
                    nxtNode,
                } = this.resizingData
                const diffX = e.pageX - currPageX
                const targetNewWidth = targetNodeWidth + diffX
                if (targetNewWidth >= targetHeaderMinWidth) {
                    targetNode.style.maxWidth = handleAddPxUnit(targetNewWidth)
                    targetNode.style.minWidth = handleAddPxUnit(targetNewWidth)
                    if (nxtNode) {
                        nxtNode.style.maxWidth = 'unset'
                        nxtNode.style.minWidth = 'unset'
                    }
                }
            }
        },
        resizerMouseUp() {
            if (this.isResizing) {
                this.isResizing = false
                this.computeHeaderWidthMap()
                this.resizingData = null
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
        &:last-child {
            padding-right: 0px !important;
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
