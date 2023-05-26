<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr v-model="graphConfigData" :height="toolbarHeight" />
        <mxs-erd
            ref="diagram"
            :ctrDim="diagramDim"
            :data="graphData"
            :graphConfigData="graphConfigData"
            v-on="$listeners"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import ErdTask from '@wsModels/ErdTask'
import ErToolbarCtr from '@wkeComps/ErdWke/ErToolbarCtr.vue'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'

export default {
    name: 'diagram-ctr',
    components: { ErToolbarCtr },
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            graphConfigData: {
                link: {
                    color: '#424f62',
                    strokeWidth: 1,
                    isAttrToAttr: false,
                    opacity: 1,
                    [EVENT_TYPES.HOVER]: { color: 'white', invisibleOpacity: 1 },
                    [EVENT_TYPES.DRAGGING]: { color: 'white', invisibleOpacity: 1 },
                },
                marker: { width: 18 },
                linkShape: {
                    type: LINK_SHAPES.ORTHO,
                    entitySizeConfig: { rowHeight: 32, rowOffset: 4, headerHeight: 32 },
                },
            },
        }
    },
    computed: {
        graphData() {
            return ErdTask.getters('getActiveGraphData') || {}
        },
        diagram() {
            return this.$refs.diagram
        },
        toolbarHeight() {
            return 28
        },
        diagramDim() {
            return { width: this.dim.width, height: this.dim.height - this.toolbarHeight }
        },
    },
}
</script>
