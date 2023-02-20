<template>
    <div class="mxs-split-pane-container" :style="{ cursor, ...resizingStyle }">
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
        value: { type: Number, required: true },
        boundary: { type: Number, required: true }, // boundary value in pixel unit
        minPercent: { type: Number, default: 0 },
        maxPercent: { type: Number, default: 100 },
        /**
         * The value for each prop must meet the condition to take effect
         * deactivatedMinPctZone > minPercent
         * deactivatedMaxPctZone < maxPercent
         * If the props are not used, it will use minPercent/maxPercent as the value
         */
        deactivatedMinPctZone: { type: Number },
        deactivatedMaxPctZone: { type: Number },
        split: {
            type: String,
            validator(value) {
                return ['vert', 'horiz'].indexOf(value) !== -1
            },
            required: true,
        },
        disable: { type: Boolean, default: false },
        revertRender: { type: Boolean, default: false },
        /**
         * let the resize action continue but the value props is stopped at minPercent or maxPercent.
         * This emits @resizing with value beyond minPercent/maxPercent threshold
         */
        progress: { type: Boolean, default: false },
    },
    data() {
        return {
            active: false,
            currPct: 0,
            // initial value when mousedown
            startPoint: 0,
            initialValueInPx: 0,
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
        deactMinPctZone() {
            return this.deactivatedMinPctZone || this.minPercent
        },
        deactMaxPctZone() {
            return this.deactivatedMaxPctZone || this.maxPercent
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
            // update current percent when value change. e.g when pane is toggled
            if (v !== this.currPct) this.currPct = v
        },
    },
    created() {
        this.currPct = this.value
    },
    methods: {
        addEvents() {
            window.addEventListener('mousemove', this.onMouseMove)
            window.addEventListener('mouseup', this.onMouseUp)
        },
        rmEvents() {
            window.removeEventListener('mousemove', this.onMouseMove)
            window.removeEventListener('mouseup', this.onMouseUp)
        },
        onMouseDown(e) {
            this.initialValueInPx = (this.currPct * this.boundary) / 100
            this.startPoint = this.isVertSplit ? e.pageX : e.pageY
            this.addEvents()
            this.active = true
        },
        onMouseUp() {
            this.rmEvents()
            this.active = false
        },
        calCurrPct(offset) {
            return ((this.initialValueInPx + offset) / this.boundary) * 100
        },
        isInThreshold(v) {
            return v <= this.minPercent && v >= this.maxPercent
        },
        /**
         * prevent currPct from having value beyond the minPercent threshold
         * currPct stops updating when the percent <= minPercent
         */
        handleBrakeOnMinPct(v) {
            if (v <= this.minPercent) this.currPct = this.minPercent
        },
        /**
         * prevent currPct from having value beyond the maxPercent threshold
         * currPct stops updating when the percent >= maxPercent
         */
        handleBrakeOnMaxPct(v) {
            if (v >= this.maxPercent) this.currPct = this.maxPercent
        },

        onMouseMove(e) {
            const endpoint = this.isVertSplit ? e.pageX : e.pageY
            const offset = endpoint - this.startPoint
            let percent = this.calCurrPct(offset)

            // stop updating currPct on deactivated zone but continue if it reaches minPercent/maxPercent
            if (this.deactMinPctZone && percent <= this.deactMinPctZone)
                this.handleBrakeOnMinPct(percent)
            else if (this.deactMaxPctZone && percent >= this.deactMaxPctZone)
                this.handleBrakeOnMaxPct(percent)
            else if (this.isInThreshold(percent)) {
                this.handleBrakeOnMinPct(percent)
                this.handleBrakeOnMaxPct(percent)
            } else this.currPct = percent

            // emit event that returns value beyond the minPercent/maxPercent threshold
            if (this.progress) this.$emit('resizing', this.calCurrPct(offset))
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
