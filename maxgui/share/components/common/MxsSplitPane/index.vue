<template>
    <div
        class="mxs-split-pane-container"
        :style="{ cursor, ...resizingStyle }"
        @mouseup="onMouseUp"
        @mousemove="onMouseMove"
    >
        <pane :isLeft="!revertRender" :split="split" :style="leftPaneStyle">
            <slot :name="`pane-${revertRender ? 'right' : 'left'}`" />
        </pane>
        <pane :isLeft="revertRender" :split="split" :style="rightPaneStyle">
            <slot :name="`pane-${revertRender ? 'left' : 'right'}`" />
        </pane>
        <resizer
            :style="resizerStyle"
            :split="split"
            :active="active"
            :disable="disable"
            @mousedown.native="onMouseDown"
        />
        <div v-if="active" class="resizing-mask" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Resizer from './Resizer.vue'
import Pane from './Pane.vue'
export default {
    name: 'mxs-split-pane',
    components: { Resizer, Pane },
    props: {
        value: { type: Number, default: 50 },
        minPercent: { type: Number, default: 0 },
        maxPercent: { type: Number, default: 100 },
        split: {
            validator(value) {
                return ['vert', 'horiz'].indexOf(value) !== -1
            },
            required: true,
        },
        disable: { type: Boolean, default: false },
        revertRender: { type: Boolean, default: false },
        /**
         * let the resize action continue but the value props is stopped at minPercent or maxPercent.
         * This emits @resizing with current percent value that go beyond min/max threshold
         */
        progress: { type: Boolean, default: false },
    },
    data() {
        return {
            active: false,
            currPct: 0,
        }
    },
    computed: {
        isVertSplit() {
            return this.split === 'vert'
        },
        panePosType() {
            return this.isVertSplit ? 'width' : 'height'
        },
        resizerPosType() {
            return this.isVertSplit ? 'left' : 'top'
        },
        leftPanelPos() {
            return { [this.panePosType]: `${this.currPct}%` }
        },
        rightPanelPos() {
            return { [this.panePosType]: `${100 - this.currPct}%` }
        },
        resizerStyle() {
            let style = {
                [this.resizerPosType]: `${this.currPct}%`,
                ...this.resizingStyle,
            }
            if (this.disable) {
                style.cursor = 'unset'
                style.pointerEvents = 'none'
            }
            return style
        },
        cursor() {
            return this.active ? (this.split === 'vert' ? 'col-resize' : 'row-resize') : ''
        },
        leftPaneStyle() {
            return this.revertRender ? this.rightPanelPos : this.leftPanelPos
        },
        rightPaneStyle() {
            return this.revertRender ? this.leftPanelPos : this.rightPanelPos
        },
        resizingStyle() {
            return {
                pointerEvents: this.active ? 'all !important' : 'auto',
            }
        },
    },
    watch: {
        active(v, oV) {
            // active true when user resizing, false when finish it
            if (oV && !v) {
                // only emit when user finishes resizing
                this.$emit('input', this.currPct)
            }
            if (v) document.body.classList.add('no-userSelect--all', 'no-pointerEvent--all')
            else document.body.classList.remove('no-userSelect--all', 'no-pointerEvent--all')
        },
        value(v) {
            // update current pecent when value change. e.g when pane is toggled
            if (v !== this.currPct) this.currPct = v
        },
    },
    created() {
        this.currPct = this.value
    },

    methods: {
        onMouseDown() {
            this.active = true
        },
        onMouseUp() {
            this.active = false
        },
        onMouseMove(e) {
            if (e.buttons === 0 || e.which === 0) this.active = false
            if (this.active) {
                let offset = 0
                let target = e.currentTarget
                if (this.isVertSplit)
                    while (target) {
                        offset += target.offsetLeft
                        target = target.offsetParent
                    }
                else
                    while (target) {
                        offset += target.offsetTop
                        target = target.offsetParent
                    }
                const currPage = this.isVertSplit ? e.pageX : e.pageY
                const { currentTarget: { offsetWidth, offsetHeight } = {} } = e
                const targetOffset = this.isVertSplit ? offsetWidth : offsetHeight
                const percent = Math.floor(((currPage - offset) / targetOffset) * 10000) / 100
                const matchThreshold = percent >= this.minPercent && percent <= this.maxPercent
                if (matchThreshold) this.currPct = percent
                if (this.progress) this.$emit('resizing', percent)
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.mxs-split-pane-container {
    height: 100%;
    position: relative;
}
</style>
