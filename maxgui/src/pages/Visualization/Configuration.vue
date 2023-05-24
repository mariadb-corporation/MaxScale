<template>
    <div class="d-flex flex-column fill-height">
        <page-header-right showCreateNew @on-count-done="fetchConfigData" />
        <v-card ref="wrapper" v-resize.quiet="setCtrDim" class="fill-height graph-card" outlined>
            <mxs-dag-graph
                v-if="ctrDim.height && config_graph_data.length"
                :data="config_graph_data"
                :dim="ctrDim"
                :nodeWidth="nodeWidth"
                dynNodeHeight
                revert
                draggable
                :colorizingLinkFn="colorizingLinkFn"
                :handleRevertDiagonal="handleRevertDiagonal"
            >
                <template v-slot:rect-node-content="{ data: { node, recompute, isDragging } }">
                    <conf-node
                        v-if="!$typy(node, 'data').isEmptyObject"
                        :class="{ 'no-pointerEvent': isDragging }"
                        :node="node"
                        :nodeWidth="nodeWidth"
                        :recompute="recompute"
                    />
                </template>
            </mxs-dag-graph>
        </v-card>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import PageHeaderRight from './PageHeaderRight'
import ConfNode from './ConfNode.vue'
export default {
    name: 'configuration',
    components: {
        'page-header-right': PageHeaderRight,
        'conf-node': ConfNode,
    },
    data() {
        return {
            //states for mxs-dag-graph
            ctrDim: {},
            nodeWidth: 220,
            dynNodeHeightMap: {},
        }
    },
    computed: {
        ...mapState({
            config_graph_data: state => state.visualization.config_graph_data,
            RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES,
        }),
    },
    async created() {
        await this.fetchConfigData()
    },
    mounted() {
        this.$nextTick(() => this.setCtrDim())
    },
    methods: {
        ...mapActions({
            fetchConfigData: 'visualization/fetchConfigData',
        }),
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.wrapper.$el
            this.ctrDim = { width: clientWidth, height: clientHeight }
        },
        colorizingLinkFn({ source, target }) {
            const sourceType = source.data.type
            const targetType = target.data.type
            const { SERVICES, SERVERS, MONITORS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (sourceType) {
                case MONITORS:
                    if (targetType === SERVERS || targetType === SERVERS) return '#0E9BC0'
                    else if (targetType === SERVICES) return '#7dd012'
                    break
                case SERVERS:
                    if (targetType === SERVICES) return '#7dd012'
                    break
                case SERVICES:
                    if (targetType === LISTENERS) return '#424f62'
                    else if (targetType === SERVICES) return '#7dd012'
            }
        },
        handleRevertDiagonal({ source, target }) {
            const sourceType = source.data.type
            const targetType = target.data.type
            const { SERVICES, SERVERS, MONITORS, LISTENERS } = this.RELATIONSHIP_TYPES
            switch (sourceType) {
                case MONITORS:
                case SERVERS:
                    if (targetType === SERVICES) return true
                    break
                case SERVICES:
                    if (targetType === LISTENERS || targetType === SERVICES) return true
                    break
            }
            return false
        },
    },
}
</script>
