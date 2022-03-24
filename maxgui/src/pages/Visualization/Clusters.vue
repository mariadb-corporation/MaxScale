<template>
    <v-container v-if="!$typy(clusters).isEmptyObject" fluid>
        <page-header-right @on-count-done="discoveryClusters" />
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
                                <icon-sprite-sheet
                                    size="13"
                                    class="server-state-icon mr-1"
                                    :frame="
                                        $help.serverStateIcon(
                                            $typy(
                                                cluster,
                                                'children[0].serverData.attributes.state'
                                            ).safeString
                                        )
                                    "
                                >
                                    status
                                </icon-sprite-sheet>
                                <truncate-string :text="cluster.children[0].name" />
                            </v-list-item-subtitle>
                        </v-list-item>
                        <v-list-item>
                            <v-list-item-title class="text-subtitle-2 color text-navigation">
                                <span class="text-uppercase">{{ $tc('slaves', 2) }} </span>:
                            </v-list-item-title>
                            <v-list-item-subtitle class="text-right">
                                <!-- TODO: hover on chip will show info about the server-->
                                <v-chip
                                    v-for="(item, stateType) in groupSlaveServersByStateType(
                                        cluster
                                    )"
                                    :key="stateType"
                                    :color="stateType"
                                    text-color="white"
                                    class="ml-1 lighten-1"
                                    small
                                >
                                    <v-avatar
                                        style="margin-left: -12px; border-radius: 50% 0 0;"
                                        :class="`color bg-${stateType}`"
                                        left
                                    >
                                        <strong>{{ item.servers.length }}</strong>
                                    </v-avatar>
                                    <span class="text-lowercase">{{ $t(item.label) }} </span>
                                </v-chip>
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
import PageHeaderRight from './PageHeaderRight'
export default {
    name: 'clusters',
    components: {
        'page-header-right': PageHeaderRight,
    },
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
        /**
         * Group servers with the same states together.
         * The state type is determined by using $help.serverStateIcon
         * @param {Object} cluster
         * @return {Object}
         */
        groupSlaveServersByStateType(cluster) {
            let group = {}
            const master = this.$typy(cluster, 'children[0]').safeObject
            if (master) {
                this.$typy(master, 'children').safeArray.forEach(n => {
                    const groupName = this.getServerStateType(n.serverData.attributes.state)
                    const label = this.labellingStateType(groupName)
                    if (!group[groupName]) group[groupName] = { label, servers: [] }
                    group[groupName].servers.push(n)
                })
            }
            return group
        },
        getServerStateType(state) {
            switch (this.$help.serverStateIcon(state)) {
                case 0:
                    return 'error'
                case 1:
                    return 'success'
                case 2:
                    return 'warning'
            }
        },
        labellingStateType(stateType) {
            switch (stateType) {
                case 'error':
                    return 'down'
                case 'success':
                    return 'up'
                // warning when server is in maintenance state
                case 'warning':
                    return 'maintenance'
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.cluster-card:not(:hover) {
    box-shadow: 1px 1px 7px rgba(0, 0, 0, 0.1);
}
</style>
