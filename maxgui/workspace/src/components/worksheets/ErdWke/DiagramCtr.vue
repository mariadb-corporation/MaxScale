<template>
    <div class="fill-height relative">
        <er-toolbar-ctr class="absolute" />
        <mxs-erd :ctrDim="dim" :data="graphData" :graphConfigData="graphConfigData" />
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
            strokeWidth: 1,
            //TODO: Add inputs to change below values
            isAttrToAttr: false,
            linkShapeType: LINK_SHAPES.ORTHO,
        }
    },
    computed: {
        entitySizeConfig() {
            return { rowHeight: 32, rowOffset: 4, headerHeight: 32 }
        },
        graphConfigData() {
            return {
                link: {
                    color: '#424f62',
                    strokeWidth: this.strokeWidth,
                    opacity: 1,
                    dashArr: '5',
                    isAttrToAttr: this.isAttrToAttr,
                },
                marker: { width: 18 },
                linkShape: {
                    type: this.linkShapeType,
                    entitySizeConfig: this.entitySizeConfig,
                },
            }
        },
        graphData() {
            return ErdTask.getters('getActiveGraphData') || {}
        },
    },
}
</script>
