<template>
    <div class="d-flex flex-column fill-height">
        <v-card ref="wrapper" v-resize.quiet="setCtrDim" class="fill-height graph-card" outlined>
            <dag-graph
                v-if="ctrDim.height && config_graph_data.length"
                :data="config_graph_data"
                :dim="ctrDim"
                :nodeSize="nodeSize"
                :dynNodeHeightMap="dynNodeHeightMap"
                revert
                :colorizingLinkFn="colorizingLinkFn"
            >
                <template v-slot:rect-node-content="{ data: { node } }">
                    <conf-node
                        v-if="!$typy(node, 'data').isEmptyObject"
                        :node="node"
                        :nodeSize="nodeSize"
                        @node-height="$set(dynNodeHeightMap, node.data.id, $event)"
                    />
                </template>
            </dag-graph>
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
 * Change Date: 2026-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import ConfNode from './ConfNode.vue'
export default {
    name: 'configuration',
    components: {
        'conf-node': ConfNode,
    },
    data() {
        return {
            //states for tree-graph
            ctrDim: {},
            defNodeHeight: 100,
            defNodeWidth: 220,
            dynNodeHeightMap: {},
        }
    },
    computed: {
        ...mapState({
            config_graph_data: state => state.visualization.config_graph_data,
            RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES,
        }),
        maxNodeHeight() {
            const v = Math.max(...Object.values(this.dynNodeHeightMap).map(n => n.height))
            if (this.$typy(v).isNumber) return v
            return this.defNodeHeight
        },
        nodeSize() {
            return { width: this.defNodeWidth, height: this.maxNodeHeight }
        },
    },
    async created() {
        this.$nextTick(() => this.setCtrDim())
        await this.fetchConfigData()
    },
    methods: {
        ...mapActions({
            fetchConfigData: 'visualization/fetchConfigData',
        }),
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.wrapper.$el
            this.ctrDim = { width: clientWidth, height: clientHeight - 2 }
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
    },
}
</script>
