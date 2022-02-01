<template>
    <page-wrapper>
        <portal to="page-header">
            <div class="d-flex align-center">
                <v-btn class="ml-n4" icon @click="goBack">
                    <v-icon
                        class="mr-1"
                        style="transform:rotate(90deg)"
                        size="28"
                        color="deep-ocean"
                    >
                        $vuetify.icons.arrowDown
                    </v-icon>
                </v-btn>
                <div class="d-inline-flex align-center">
                    <truncate-string :text="$route.params.id" :maxWidth="600">
                        <span
                            style="line-height: normal;"
                            class="ml-1 mb-0 color text-navigation text-h4 page-title"
                        >
                            {{ $route.params.id }}
                        </span>
                    </truncate-string>
                </div>
            </div>
            <div class="pl-6">
                <icon-sprite-sheet
                    size="13"
                    class="status-icon mr-1"
                    :frame="$help.monitorStateIcon(current_cluster.state)"
                >
                    status
                </icon-sprite-sheet>
                <span class="resource-state color text-navigation text-body-2">
                    {{ current_cluster.state }}
                </span>
                <span class="color text-field-text text-body-2">
                    |
                    <span class="resource-module">{{ current_cluster.module }}</span>
                </span>
            </div>
        </portal>
        <v-card
            ref="graphContainer"
            v-resize.quiet="setCtrDim"
            class="ml-6 mt-9 fill-height graph-card"
            outlined
        >
            <tree-graph
                v-if="ctrDim.height"
                :style="{ width: `${ctrDim.width}px`, height: `${ctrDim.height}px` }"
                :data="graphData"
                :dim="ctrDim"
                :nodeSize="[125, 320]"
            >
                <template v-slot:rect-node-content="{ data: { node } }">
                    <v-card outlined class="server-node" width="273" height="88">
                        <div class="d-flex align-center flex-row node-title-wrapper px-2 py-1">
                            <icon-sprite-sheet
                                size="13"
                                class="mr-1 status-icon"
                                :frame="$help.serverStateIcon($typy(node, 'data.state').safeString)"
                            >
                                status
                            </icon-sprite-sheet>
                            <div class="text-truncate">
                                <router-link
                                    :to="`/dashboard/servers/${node.id}`"
                                    class="rsrc-link"
                                >
                                    {{ $typy(node, 'data.title').safeString }}
                                </router-link>
                            </div>
                            <v-spacer />
                            <div class="button-container">
                                <!--TODO: open a dialog to config the node -->
                                <v-btn small class="ml-2 gear-btn" icon>
                                    <v-icon size="16" color="primary">
                                        $vuetify.icons.settings
                                    </v-icon>
                                </v-btn>
                            </div>
                        </div>
                        <v-divider />
                        <div class="d-flex justify-center flex-column node-text-wrapper px-2 py-1">
                            <div class="d-flex flex-row flex-grow-1">
                                <span>{{ node.data.state }}</span>
                                <v-spacer />
                                <v-tooltip
                                    top
                                    transition="slide-y-transition"
                                    content-class="shadow-drop color text-navigation py-1 px-4"
                                >
                                    <template v-slot:activator="{ on }">
                                        <span
                                            v-if="
                                                !$typy(node.data).isEmptyObject &&
                                                    !$typy(node, 'data.isMaster').safeBoolean
                                            "
                                            class="ml-1 color text-field-text"
                                            v-on="on"
                                        >
                                            (+{{ getSBM(node.data) }}s)
                                        </span>
                                    </template>
                                    <span>
                                        <!-- TODO: Show Replication status by re-using rep-tooltip -->
                                        {{ $t('repLag') }}
                                    </span>
                                </v-tooltip>
                            </div>
                            <div class="d-flex flex-grow-1">
                                <span class="text-capitalize">{{ $tc('connections', 2) }}:</span>
                                <span class="ml-1">
                                    {{ node.data.connections }}
                                </span>
                            </div>
                        </div>
                    </v-card>
                </template>
            </tree-graph>
        </v-card>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import TreeGraph from './TreeGraph.vue'
import goBack from 'mixins/goBack'

export default {
    name: 'cluster',
    components: {
        'tree-graph': TreeGraph,
    },
    mixins: [goBack],
    data() {
        return {
            ctrDim: {},
        }
    },
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
        }),
        graphData() {
            return this.$typy(this.current_cluster, 'children[0]').safeObjectOrEmpty
        },
    },
    async created() {
        this.$nextTick(() => this.setCtrDim())
        if (this.$typy(this.current_cluster).isEmptyObject)
            await this.fetchClusterById(this.$route.params.id)
    },
    methods: {
        ...mapActions({
            fetchClusterById: 'visualization/fetchClusterById',
        }),
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.graphContainer.$el
            this.ctrDim = { width: clientWidth, height: clientHeight - 2 }
        },
        getSBM(node) {
            return this.$help.getOverallRepStat({
                repStats: node.server_info.slave_connections,
                pickBy: 'seconds_behind_master',
                isNumber: true,
            })
        },
    },
}
</script>

<style lang="scss" scoped>
.server-node {
    font-size: 12px;
}
.graph-card {
    border: 1px solid #e3e6ea !important;
    box-sizing: content-box !important;
}
</style>
