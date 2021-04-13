<template>
    <div
        :style="{ cursor, userSelect }"
        class="split-pane-container"
        @mouseup="onMouseUp"
        @mousemove="onMouseMove"
    >
        <pane isLeft :split="split" :style="leftPanelPos">
            <slot name="pane-left" />
        </pane>
        <resizer
            :style="resizerPos"
            :split="split"
            :active="active"
            @mousedown.native="onMouseDown"
        />
        <pane :split="split" :style="rightPanelPos">
            <slot name="pane-right" />
        </pane>
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
        defaultPercent: { type: Number, default: 50 },
        split: {
            validator(value) {
                return ['vert', 'horiz'].indexOf(value) !== -1
            },
            required: true,
        },
    },
    data() {
        return {
            active: false,
            percent: this.defaultPercent,
        }
    },
    computed: {
        isVertSplit() {
            return this.split === 'vert'
        },
        userSelect() {
            return this.active ? 'none' : ''
        },
        cursor() {
            if (!this.active) return ''
            return this.isVertSplit ? 'col-resize' : 'row-resize'
        },
        panePosType() {
            return this.isVertSplit ? 'width' : 'height'
        },
        resizerPosType() {
            return this.isVertSplit ? 'left' : 'top'
        },
        leftPanelPos() {
            return { [this.panePosType]: `${this.percent}%` }
        },
        rightPanelPos() {
            return { [this.panePosType]: `${100 - this.percent}%` }
        },
        resizerPos() {
            return { [this.resizerPosType]: `${this.percent}%` }
        },
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
                if (percent > this.minPercent && percent < 100 - this.minPercent)
                    this.percent = percent
                this.$emit('on-resize', this.percent)
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
