<template>
    <div
        class="split-pane-container"
        :class="{ 'no-userSelect': active }"
        :style="{ cursor }"
        @mouseup="onMouseUp"
        @mousemove="onMouseMove"
    >
        <pane isLeft :split="split" :style="leftPanelPos">
            <slot name="pane-left" />
        </pane>
        <resizer
            :style="resizerStyle"
            :split="split"
            :active="active"
            :disable="disable"
            @mousedown.native="onMouseDown"
        />
        <pane :split="split" :style="rightPanelPos">
            <slot name="pane-right" />
        </pane>
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
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Resizer from './Resizer.vue'
import Pane from './Pane.vue'
export default {
    name: 'split-pane',
    components: { Resizer, Pane },
    props: {
        minPercent: { type: Number, default: 10 },
        value: { type: Number, default: 50 },
        split: {
            validator(value) {
                return ['vert', 'horiz'].indexOf(value) !== -1
            },
            required: true,
        },
        disable: { type: Boolean, default: false },
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
            return {
                [this.resizerPosType]: `${this.currPct}%`,
                ...(this.disable && { cursor: 'unset', pointerEvents: 'none' }),
            }
        },
        cursor() {
            return this.active ? (this.split === 'vert' ? 'col-resize' : 'row-resize') : ''
        },
    },
    watch: {
        active(v, oV) {
            // active true when user resizing, false when finish it
            if (oV && !v) {
                // only emit when user finishes resizing
                this.$emit('input', this.currPct)
            }
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
                if (percent >= this.minPercent && percent <= 100 - this.minPercent)
                    this.currPct = percent
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.split-pane-container {
    height: 100%;
    position: relative;
}
</style>
