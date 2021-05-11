<template>
    <div class="virtual-table__header">
        <div class="thead d-inline-block" :style="{ width: headerWidth }">
            <div class="tr" :style="{ lineHeight: $parent.lineHeight }">
                <div
                    v-for="(header, i) in headers"
                    :key="`${header}_${i}`"
                    :ref="`header__${i}`"
                    :style="{
                        ...headerStyle,
                        height: $parent.lineHeight,
                        maxWidth: `${headerWidthMap[i]}px`,
                    }"
                    class="th px-3"
                >
                    <truncate-string
                        :text="`${header}`.toUpperCase()"
                        :maxWidth="headerWidthMap[i] - 24"
                    />
                    <div
                        v-if="i !== headers.length - 1"
                        class="header__resizer d-inline-block fill-height"
                        @mousedown="e => resizerMouseDown(e, i)"
                    />
                </div>
            </div>
        </div>
        <div
            :style="{ minWidth: `${getScrollbarWidth()}px` }"
            class="d-inline-block dummy-header"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'table-header',
    props: {
        headers: { type: Array, require: true },
        boundingWidth: { type: Number, require: true },
        headerStyle: { type: Object, require: true },
    },
    data() {
        return {
            headerWidthMap: {},
            isResizing: false,
            currCol: null,
            nxtCol: null,
            currPageX: 0,
            nxtColWidth: 0,
            currColWidth: 0,
            currColIndex: 0,
        }
    },
    computed: {
        headerWidth() {
            return `calc(100% - ${this.getScrollbarWidth()}px)`
        },
    },
    watch: {
        headers() {
            // reset width to unset then get width and assign
            this.$help.doubleRAF(() => this.resetHeaderWidth())
            this.$help.doubleRAF(() => this.assignHeaderWidthMap())
        },
        boundingWidth() {
            this.resetHeaderWidth()
            this.$help.doubleRAF(() => this.assignHeaderWidthMap())
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
        resetHeaderWidth() {
            if (this.$refs[`header__${0}`])
                // set all header maxWidth to unset to get auto width in doubleRAF cb
                for (let i = 0; i < this.headers.length; i++) {
                    this.$refs[`header__${i}`][0].style.maxWidth = 'unset'
                }
        },
        resizerMouseDown(e, i) {
            this.currColIndex = i
            this.currCol = e.target.parentElement
            this.nxtCol = this.currCol.nextElementSibling
            this.currPageX = e.pageX
            this.currColWidth = this.currCol.offsetWidth
            if (this.nxtCol) this.nxtColWidth = this.nxtCol.offsetWidth
            this.isResizing = true
        },
        resizerMouseMove(e) {
            if (this.isResizing) {
                const diffX = e.pageX - this.currPageX
                if (this.currColWidth + diffX >= 28) {
                    this.currCol.style.maxWidth = `${this.currColWidth + diffX}px`
                    if (this.nxtCol && this.nxtColWidth - diffX >= 28) {
                        this.nxtCol.style.maxWidth = `${this.nxtColWidth - diffX}px`
                    }
                    this.$help.doubleRAF(() => this.assignHeaderWidthMap())
                }
            }
        },
        resizerMouseUp() {
            this.isResizing = false
            this.currPageX = 0
            this.currCol = null
            this.nxtCol = null
            this.nxtColWidth = 0
            this.currColWidth = 0
            this.currColIndex = 0
        },
        assignHeaderWidthMap() {
            if (this.$refs[`header__${0}`]) {
                let headerWidthMap = {}
                // get width of each header then use it to set same width of corresponding cells
                for (let i = 0; i < this.headers.length; i++) {
                    const headerWidth = this.$refs[`header__${i}`][0].clientWidth
                    headerWidthMap = {
                        ...headerWidthMap,
                        [i]: headerWidth,
                    }
                }
                this.headerWidthMap = headerWidthMap
            }
        },

        /**
         * This function is not working on macOs as the scrollbar is only showed when scrolling.
         * However, on Macos, scrollbar is placed above the content (overlay) instead of taking up space
         * of the content. So in macOs, this returns 0.
         * @returns {Number} scrollbar width
         */
        getScrollbarWidth() {
            // Creating invisible container
            const outer = document.createElement('div')
            outer.style.visibility = 'hidden'
            outer.style.overflow = 'scroll' // forcing scrollbar to appear
            outer.style.msOverflowStyle = 'scrollbar' // needed for WinJS apps
            document.body.appendChild(outer)

            // Creating inner element and placing it in the container
            const inner = document.createElement('div')
            outer.appendChild(inner)

            // Calculating difference between container's full width and the child width
            const scrollbarWidth = outer.offsetWidth - inner.offsetWidth

            // Removing temporary elements from the DOM
            outer.parentNode.removeChild(outer)

            return scrollbarWidth
        },
    },
}
</script>

<style lang="scss" scoped>
.virtual-table__header {
    height: 30px;
    overflow: hidden;
    background-color: $table-border;
    .tr {
        display: flex;
        flex-direction: row;
        box-shadow: -7px 5px 7px -7px rgb(0 0 0 / 10%);
        flex-wrap: nowrap;
        .th {
            display: flex;
            position: relative;
            z-index: 1;
            flex: 1;
            font-weight: bold;
            font-size: 0.75rem;
            color: $small-text;
            background-color: $table-border;
            text-transform: uppercase;
            border-bottom: none;
            &:first-child {
                border-radius: 5px 0 0 0;
            }
            &:last-child {
                border-radius: 0 5px 0 0;
            }
            .header__resizer {
                position: absolute;
                right: 0px;
                width: 11px;
                border-right: 1px solid $background;
                cursor: ew-resize;
                &--hovered,
                &:hover {
                    border-right: 3px solid $background;
                }
            }
        }
    }
    .dummy-header {
        z-index: 2;
        background-color: $table-border;
        height: 30px;
        position: absolute;
    }
}
</style>
