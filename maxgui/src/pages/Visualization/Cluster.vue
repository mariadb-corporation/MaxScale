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
                :data="$typy(current_cluster, 'children[0]').safeObjectOrEmpty"
                :dim="ctrDim"
            >
                <template v-slot:rect-node-content="{ data: { id } }">
                    <div
                        class="d-flex flex-column justify-center fill-height px-4 py-2 server-node"
                    >
                        <truncate-string :text="id" />
                    </div>
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
    },
    created() {
        this.$nextTick(() => this.setCtrDim())
        if (this.$typy(this.current_cluster).isEmptyObject)
            this.fetchClusterById(this.$route.params.id)
    },
    methods: {
        ...mapActions({
            fetchClusterById: 'visualization/fetchClusterById',
        }),
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.graphContainer.$el
            this.ctrDim = { width: clientWidth, height: clientHeight - 2 }
        },
    },
}
</script>

<style lang="scss" scoped>
.server-node {
    font-size: 14px;
}
.graph-card {
    border: 1px solid #e3e6ea !important;
    box-sizing: content-box !important;
}
</style>
