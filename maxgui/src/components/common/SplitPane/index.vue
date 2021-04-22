<template>
    <div
        ref="paneContainer"
        class="split-pane-container"
        :style="{ userSelect: active ? 'none' : '' }"
        @mouseup="onMouseUp"
        @mousemove="onMouseMove"
    >
        <pane ref="paneLeft" isLeft :split="split" :style="leftPanelPos">
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
        <div v-if="active" class="split-pane-container__mask" />
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
            dim: {
                paneL_width: '',
                paneL_height: '',
                paneR_width: '',
                paneR_height: '',
            },
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
    },
    watch: {
        active(v, oV) {
            // active true when user resizing, false when finish it
            if (oV && !v) {
                // only emit when user finishes resizing
                this.$emit('input', this.currPct)
                this.$emit('get-panes-dim', this.dim)
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
    mounted() {
        this.setInitDim()
    },
    methods: {
        setInitDim() {
            const { clientHeight: contHeight, clientWidth: contWidth } = this.$refs.paneContainer
            if (this.isVertSplit) {
                const { clientWidth: paneL_width } = this.$refs.paneLeft.$el
                this.dim.paneL_width = paneL_width
                this.dim.paneL_height = contHeight
                this.dim.paneR_width = contWidth - paneL_width
                this.dim.paneR_height = contHeight
            } else {
                const { clientHeight: paneL_height } = this.$refs.paneLeft.$el
                this.dim.paneL_width = contWidth
                this.dim.paneL_height = paneL_height
                this.dim.paneR_width = contWidth
                this.dim.paneR_height = contHeight - paneL_height
            }
            this.$emit('get-panes-dim', this.dim)
        },
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
                if (percent >= this.minPercent && percent <= 100 - this.minPercent) {
                    this.currPct = percent
                    const {
                        clientHeight: contHeight,
                        clientWidth: contWidth,
                    } = this.$refs.paneContainer

                    if (this.isVertSplit) {
                        this.dim.paneL_width = currPage - offset
                        this.dim.paneL_height = contHeight
                        this.dim.paneR_width = contWidth - this.dim.paneL_width
                        this.dim.paneR_height = contHeight
                    } else {
                        this.dim.paneL_width = contWidth
                        this.dim.paneL_height = currPage - offset
                        this.dim.paneR_width = contWidth
                        this.dim.paneR_height = contHeight - this.dim.paneL_height
                    }
                }
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.split-pane-container {
    height: 100%;
    position: relative;
    &__mask {
        z-index: 9999;
        width: 100%;
        height: 100%;
        position: absolute;
        top: 0;
        left: 0;
    }
}
</style>
