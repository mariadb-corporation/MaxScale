<template>
    <div class="fill-height relative">
        <er-toolbar-ctr v-model="graphConfigData" class="absolute" />
        <mxs-erd ref="diagram" :ctrDim="dim" :data="graphData" :graphConfigData="graphConfigData" />
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
                    opacity: 1,
                    dashArr: '5',
                    isAttrToAttr: false,
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
    },
    watch: {
        graphConfigData: {
            deep: true,
            handler(v, oV) {
                /**
                 * Because only one attribute can be changed at a time, so it's safe to
                 * access the diff with hard-code indexes.
                 */
                const diff = this.$typy(this.$helpers.deepDiff(oV, v), '[0]').safeObjectOrEmpty
                const value = this.$helpers.lodash.objGet(v, diff.path.join('.'))

                this.diagram.graphConfig.updateConfig({
                    key: diff.path[0],
                    patch: { [diff.path[1]]: value },
                })
                if (diff.path[1] === 'isAttrToAttr') this.handleIsAttrToAttrMode(value)
                this.diagram.drawLinks()
            },
        },
    },
    methods: {
        /**
         * If value is true, the diagram shows all links including composite links for composite keys
         * @param {boolean} value
         */
        handleIsAttrToAttrMode(value) {
            const graphLinks = this.diagram.graphLinks
            if (value) this.diagram.graphLinks = this.diagram.stagingData.links
            else this.diagram.graphLinks = graphLinks.filter(link => !link.isPartOfCompositeKey)
            this.diagram.simulation.force('link').links(this.diagram.graphLinks)
        },
    },
}
</script>
