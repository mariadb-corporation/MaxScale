<template>
    <div class="mxs-graph-board fill-height">
        <v-icon class="svg-grid-bg" color="#e3e6ea">$vuetify.icons.mxs_gridBg</v-icon>
        <svg ref="svg" class="mxs-graph" :width="dim.width" height="100%">
            <g id="graph-ctr" :style="{ transform }" />
        </svg>
        <slot name="append" :data="slotData" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Events
 * get-graph-ctr(SVGElement): graph-ctr <g/> element
 */
import { select as d3Select } from 'd3-selection'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'

export default {
    name: 'graph-board',
    props: {
        dim: { type: Object, required: true },
        graphDim: { type: Object, default: () => ({ width: 0, height: 0 }) },
    },
    data() {
        return {
            graphBoardZoom: { x: 24, y: this.dim.height / 2, k: 1 },
        }
    },
    computed: {
        transform() {
            const { x, y, k } = this.graphBoardZoom
            return `translate(${x}px, ${y}px) scale(${k})`
        },
        slotData() {
            return { transform: this.transform, zoom: this.graphBoardZoom }
        },
    },
    watch: {
        graphDim: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.centerGraph()
            },
        },
    },
    mounted() {
        this.initSvg()
    },

    methods: {
        initSvg() {
            // Draw svg mxs-graph
            const svg = d3Select(this.$refs.svg)
                .call(zoom().on('zoom', e => (this.graphBoardZoom = e.transform)))
                .on('dblclick.zoom', null)
            this.centerGraph()
            this.$emit('get-graph-ctr', svg.select('g#graph-ctr'))
        },
        // Vertically and horizontally Center graph
        centerGraph() {
            this.graphBoardZoom = zoomIdentity
                .translate(
                    (this.dim.width - this.graphDim.width) / 2,
                    (this.dim.height - this.graphDim.height) / 2
                )
                .scale(1)
            // set transform
            d3Select(this.$refs.svg).call(zoom().transform, this.graphBoardZoom)
        },
    },
}
</script>

<style lang="scss" scoped>
.mxs-graph-board {
    width: 100%;
    position: relative;
    overflow: hidden;
    .svg-grid-bg {
        width: 100%;
        height: 100%;
        z-index: 1;
        pointer-events: none;
        background: transparent;
        position: absolute;
        left: 0;
    }
    .mxs-graph {
        position: relative;
        left: 0;
        z-index: 2;
    }
}
</style>
