<template>
    <v-container v-if="!$typy(clusters).isEmptyObject" fluid>
        <v-row>
            <v-col
                v-for="cluster in clusters"
                :key="cluster.id"
                class="pa-2 pb-4 pt-0"
                cols="12"
                md="6"
                lg="4"
            >
                <v-card hover outlined class="cluster-card" @click="navToCluster(cluster)">
                    <v-list-item>
                        <v-list-item-avatar v-show="cluster.state" class="mr-2 mt-n2" size="20">
                            <icon-sprite-sheet
                                size="20"
                                :frame="$help.monitorStateIcon(cluster.state)"
                            >
                                status
                            </icon-sprite-sheet>
                        </v-list-item-avatar>
                        <v-list-item-content class="py-4">
                            <v-list-item-title
                                class="tk-azo-sans-web text-h5 color font-weight-medium text-blue-azure"
                            >
                                <truncate-string :text="cluster.id" />
                            </v-list-item-title>
                            <v-list-item-subtitle>
                                <span class="field-text">
                                    {{ cluster.module }}
                                </span>
                            </v-list-item-subtitle>
                        </v-list-item-content>
                    </v-list-item>
                    <v-divider />
                    <v-list class="px-7">
                        <v-list-item>
                            <v-list-item-title class="text-subtitle-2 color text-navigation">
                                <span class="text-uppercase">{{ $t('master') }} </span>:
                            </v-list-item-title>
                            <v-list-item-subtitle class="text-right">
                                <truncate-string :text="cluster.children[0].name" />
                            </v-list-item-subtitle>
                        </v-list-item>
                        <v-list-item>
                            <v-list-item-title class="text-subtitle-2 color text-navigation">
                                <span class="text-uppercase">{{ $tc('servers', 2) }} </span>:
                            </v-list-item-title>
                            <v-list-item-subtitle class="text-right">
                                <!-- TODO: Show server's state as chips, hover on chip will show server state-->
                                <!-- number of slave + master -->
                                {{ $typy(cluster, 'children[0].children.length').safeNumber + 1 }}
                            </v-list-item-subtitle>
                        </v-list-item>
                        <!-- TODO: Determine what other information should be listed in the cluster card -->
                    </v-list>
                </v-card>
            </v-col>
        </v-row>
    </v-container>
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
import { mapState, mapActions, mapMutations } from 'vuex'
export default {
    name: 'clusters',
    computed: {
        ...mapState({
            clusters: state => state.visualization.clusters,
        }),
    },
    async created() {
        await this.discoveryClusters()
    },
    methods: {
        ...mapActions({
            discoveryClusters: 'visualization/discoveryClusters',
        }),
        ...mapMutations({
            SET_CURR_CLUSTER: 'visualization/SET_CURR_CLUSTER',
        }),
        navToCluster(cluster) {
            this.$router.push({
                path: `/visualization/clusters/${cluster.id}`,
            })
            this.SET_CURR_CLUSTER(cluster)
        },
    },
}
</script>

<style lang="scss" scoped>
.cluster-card:not(:hover) {
    box-shadow: 1px 1px 7px rgba(0, 0, 0, 0.1);
}
</style>
