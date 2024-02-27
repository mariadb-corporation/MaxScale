<template>
    <div class="d-flex flex-column fill-height">
        <page-header-right showCreateNew @on-count-done="fetchConfigData" />
        <v-card ref="wrapper" v-resize.quiet="setCtrDim" class="fill-height graph-card" outlined>
            <dag-graph
                v-if="ctrDim.height && config_graph_data.length"
                :data="config_graph_data"
                :dim="ctrDim"
                :defNodeSize="{ width: 220, height: 100 }"
                revert
                draggable
                :colorizingLinkFn="colorizingLinkFn"
                :handleRevertDiagonal="handleRevertDiagonal"
            >
                <template
                    v-slot:graph-node-content="{
                        data: { node, nodeSize, onNodeResized, isDragging },
                    }"
                >
                    <conf-node
                        :class="{ 'no-pointerEvent': isDragging }"
                        :style="{ minWidth: '220px', maxWidth: '250px' }"
                        :node="node"
                        :nodeWidth="nodeSize.width"
                        :onNodeResized="onNodeResized"
                        showFiltersInService
                    />
                </template>
            </dag-graph>
        </v-card>
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
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import PageHeaderRight from './PageHeaderRight'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'configuration',
    components: {
        'page-header-right': PageHeaderRight,
    },
    data() {
        return {
            //states for dag-graph
            ctrDim: {},
        }
    },
    computed: {
        ...mapState({ config_graph_data: state => state.visualization.config_graph_data }),
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
            const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPES
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
            const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPES
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
