<template>
  <div class="svg-graph-board-ctr fill-height">
    <VIcon class="svg-grid-bg" color="#e3e6ea" icon="mxs:gridBg" />
    <svg
      ref="svg"
      class="svg-graph-board"
      :width="dim.width"
      height="100%"
      @contextmenu.prevent="$emit('on-board-contextmenu', $event)"
    >
      <g id="graph-ctr" :style="style" />
    </svg>
    <slot name="append" :data="{ style }" />
  </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Events
 * get-graph-ctr(SVGElement): graph-ctr <g/> element
 * on-board-contextmenu(Event)
 */
import { select as d3Select } from 'd3-selection'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'

export default {
  props: {
    modelValue: { type: Object, required: true },
    dim: { type: Object, required: true },
    graphDim: { type: Object, default: () => ({ width: 0, height: 0 }) },
    scaleExtent: { type: Array, default: () => [0.25, 2] },
  },
  data() {
    return {
      svg: null,
    }
  },
  computed: {
    style() {
      const { x, y, k, transition } = this.panAndZoom
      return {
        transform: `translate(${x}px, ${y}px) scale(${k})`,
        transition: `all ${transition ? 0.3 : 0}s ease`,
      }
    },
    panAndZoom: {
      get() {
        return this.modelValue
      },
      set(v) {
        this.$emit('update:modelValue', v)
      },
    },
  },
  watch: {
    graphDim: {
      deep: true,
      handler(v, oV) {
        if (!this.$helpers.lodash.isEqual(v, oV)) this.centerGraph()
      },
    },
    panAndZoom: {
      deep: true,
      handler(v, oV) {
        if (!this.$helpers.lodash.isEqual(v, oV)) this.applyZoom(v)
      },
    },
  },
  mounted() {
    this.initSvg()
  },
  methods: {
    initSvg() {
      this.svg = d3Select(this.$refs.svg)
      this.svg
        .call(
          zoom()
            .scaleExtent(this.scaleExtent)
            .on('zoom', (e) => {
              const { x, y, k } = e.transform
              this.panAndZoom = { x, y, k, eventType: e.sourceEvent.type }
            })
        )
        .on('dblclick.zoom', null)
      this.centerGraph()
      this.$emit('get-graph-ctr', this.svg.select('g#graph-ctr'))
    },
    // Vertically and horizontally Center graph
    centerGraph() {
      const x = (this.dim.width - this.graphDim.width) / 2,
        y = (this.dim.height - this.graphDim.height) / 2,
        k = 1
      this.panAndZoom = { ...this.panAndZoom, x, y, k }
    },
    applyZoom(v) {
      this.svg.call(zoom().transform, zoomIdentity.translate(v.x, v.y).scale(v.k))
    },
  },
}
</script>

<style lang="scss" scoped>
.svg-graph-board-ctr {
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
  .svg-graph-board {
    position: relative;
    left: 0;
    z-index: 2;
  }
}
</style>
