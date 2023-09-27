<template>
    <div class="thead d-flex" :style="{ width: $helpers.handleAddPxUnit(boundingWidth) }">
        <div
            v-if="showOrderNumber"
            ref="orderNumberHeader"
            class="th relative px-3 d-inline-flex align-center"
            :style="{
                width: 'max-content',
                borderRight: '1px solid white',
            }"
        >
            #
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ rowCount }}) </span>
        </div>
        <template v-for="(header, index) in headers">
            <div
                v-if="!header.hidden"
                :id="genHeaderColID(index)"
                :key="`${header.text}_${index}`"
                :ref="`header__${index}`"
                :style="{
                    maxWidth: $helpers.handleAddPxUnit(headerWidthMap[index]),
                    minWidth: $helpers.handleAddPxUnit(headerWidthMap[index]),
                }"
                class="th relative px-3 d-inline-flex align-center flex-grow-1"
                :class="{
                    'text-capitalize': header.capitalize,
                    'text-uppercase': header.uppercase,
                    'th--resizable': !isResizerDisabled(header),
                }"
            >
                <span
                    class="text-truncate"
                    :class="{ 'label-required': header.required }"
                    :style="{ whiteSpace: 'nowrap' }"
                >
                    <slot
                        :name="`header-${header.text}`"
                        :data="{
                            header,
                            // maxWidth: minus padding and sort-icon
                            maxWidth: headerTxtMaxWidth(index),
                            index,
                            activatorID: genHeaderColID(index),
                        }"
                    >
                        {{ header.text }}
                    </slot>
                </span>
                <div
                    v-if="header.text !== $typy(lastVisHeader, 'text').safeString"
                    class="header__resizer d-inline-block fill-height"
                    v-on="
                        isResizerDisabled(header)
                            ? null
                            : { mousedown: e => resizerMouseDown(e, index) }
                    "
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
 items: {
  width?: string | number, default width when header is rendered
  maxWidth?: string | number, if maxWidth is declared, it ignores width and use it as default width
  minWidth?: string | number, allow resizing column to no smaller than provided value
  resizable?: boolean, true by default
  capitalize?: boolean, capitalize first letter of the header
  uppercase?: boolean, uppercase all letters of the header
  hidden?: boolean, hidden the column
  required?: boolean, if true, `label-required` class will be added to the header
}
 */
export default {
    name: 'table-headers',
    props: {
        items: { type: Array, required: true },
        showOrderNumber: { type: Boolean, required: true },
        rowCount: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
    },
    data() {
        return {
            orderNumberHeaderWidth: 0,
            headerWidthMap: {},
            isResizing: false,
            resizingData: null,
        }
    },
    computed: {
        headers() {
            return this.items
        },
        visHeaders() {
            return this.items.filter(h => !h.hidden)
        },
        lastVisHeader() {
            if (this.visHeaders.length) return this.visHeaders.at(-1)
            return {}
        },
        //threshold, user cannot resize header smaller than this
        headerMinWidthMap() {
            return this.headers.reduce((obj, h) => {
                obj[h.text] = h.minWidth || 67
                return obj
            }, {})
        },
    },
    watch: {
        isResizing(v) {
            this.$emit('is-resizing', v)
        },
        orderNumberHeaderWidth: {
            immediate: true,
            handler(v) {
                this.$emit('order-number-header-width', v)
            },
        },
    },
    created() {
        window.addEventListener('mousemove', this.resizerMouseMove)
        window.addEventListener('mouseup', this.resizerMouseUp)
        this.watch()
    },
    destroyed() {
        window.removeEventListener('mousemove', this.resizerMouseMove)
        window.removeEventListener('mouseup', this.resizerMouseUp)
    },
    activated() {
        this.watch()
    },
    deactivated() {
        this.unwatch()
    },
    beforeDestroy() {
        this.unwatch()
    },
    methods: {
        watch() {
            this.watch_headers()
            this.watch_boundingWidth()
            this.watch_headerWidthMap()
        },
        unwatch() {
            this.$typy(this.unwatch_headers).safeFunction()
            this.$typy(this.unwatch_boundingWidth).safeFunction()
            this.$typy(this.unwatch_headerWidthMap).safeFunction()
        },
        watch_headers() {
            this.unwatch_headers = this.$watch(
                'headers',
                (v, oV) => {
                    if (!this.$helpers.lodash.isEqual(v, oV)) this.computeWidth()
                },
                { deep: true, immediate: true }
            )
        },
        watch_boundingWidth() {
            this.unwatch_boundingWidth = this.$watch('boundingWidth', () => this.computeWidth())
        },
        watch_headerWidthMap() {
            this.unwatch_headerWidthMap = this.$watch(
                'headerWidthMap',
                v => this.$emit('get-header-width-map', v),
                { deep: true }
            )
        },
        genHeaderColID(index) {
            return `table-header__header-text_${index}`
        },
        headerTxtMaxWidth(index) {
            const w = this.$typy(this.headerWidthMap[index]).safeNumber - 24 // padding
            return w > 0 ? w : 1
        },
        isResizerDisabled(header) {
            return header.text === '#' || header.resizable === false
        },
        resetHeaderWidth() {
            let headerWidthMap = {}
            for (const [index, header] of this.headers.entries()) {
                headerWidthMap = {
                    ...headerWidthMap,
                    [index]: header.maxWidth
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
        computeOrderNumberHeaderWidth() {
            const orderNumberHeader = this.$typy(this.$refs, 'orderNumberHeader').safeObject
            if (orderNumberHeader) {
                const { width = this.headerMinWidth } = orderNumberHeader.getBoundingClientRect()
                this.orderNumberHeaderWidth = width
            }
        },
        computeHeaderWidthMap() {
            let headerWidthMap = {}
            // get width of each header then use it to set same width of corresponding cells
            for (const [index, header] of this.headers.entries()) {
                const minWidth = this.headerMinWidthMap[header.text]
                const width = this.getHeaderWidth(index)
                const headerWidth = width < minWidth ? minWidth : width
                headerWidthMap = { ...headerWidthMap, [index]: headerWidth }
            }
            this.headerWidthMap = headerWidthMap
        },
        computeWidth() {
            this.resetHeaderWidth()
            this.$nextTick(() => {
                this.computeHeaderWidthMap()
                if (this.showOrderNumber) this.computeOrderNumberHeaderWidth()
            })
        },
        hasClass({ ele, className }) {
            let str = ` ${ele.className} `
            let classToFind = ` ${className} `
            return str.indexOf(classToFind) !== -1
        },
        getNxtResizableNodeByClass({ node, className, nxtHeaderIdx }) {
            while ((node = node.nextSibling)) {
                nxtHeaderIdx++
                if (this.hasClass({ ele: node, className })) return { node, nxtHeaderIdx }
            }
            return { node: null, nxtHeaderIdx }
        },
        resizerMouseDown(e, index) {
            const { node: nxtNode, nxtHeaderIdx } = this.getNxtResizableNodeByClass({
                node: e.target.parentElement,
                className: 'th--resizable',
                nxtHeaderIdx: index,
            })
            this.resizingData = {
                currPageX: e.pageX,
                // target metadata
                targetNode: e.target.parentElement,
                targetNodeWidth: e.target.parentElement.offsetWidth,
                targetHeaderMinWidth: this.headerMinWidthMap[this.headers[index].text],
                // next resizable header node
                nxtNode,
                nxtNodeWidth: nxtNode.offsetWidth,
                nxtHeaderMinWidth: this.headerMinWidthMap[this.headers[nxtHeaderIdx].text],
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
