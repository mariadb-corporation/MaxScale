<template>
    <v-container v-if="!$typy(clusters).isEmptyObject" fluid>
        <page-header-right @on-count-done="discoveryClusters" />
        <v-row>
            <v-col v-for="cluster in clusters" :key="cluster.id" cols="12" md="6" lg="4">
                <v-card hover outlined class="cluster-card" @click="navToCluster(cluster)">
                    <v-list-item>
                        <v-list-item-avatar v-show="cluster.state" class="mr-2 mt-n2" size="20">
                            <icon-sprite-sheet
                                size="20"
                                :frame="$helpers.monitorStateIcon(cluster.state)"
                            >
                                monitors
                            </icon-sprite-sheet>
                        </v-list-item-avatar>
                        <v-list-item-content class="py-4">
                            <v-list-item-title
                                class="tk-azo-sans-web text-h5 mxs-color-helper font-weight-medium text-blue-azure d-flex"
                            >
                                <mxs-truncate-str :tooltipItem="{ txt: `${cluster.id}` }" />
                            </v-list-item-title>
                            <v-list-item-subtitle>
                                <span class="text-grayed-out">
                                    {{ cluster.module }}
                                </span>
                            </v-list-item-subtitle>
                        </v-list-item-content>
                    </v-list-item>
                    <v-divider />
                    <v-list class="px-7">
                        <v-list-item>
                            <v-list-item-title
                                class="text-subtitle-2 mxs-color-helper text-navigation"
                            >
                                <span class="text-uppercase">{{ $mxs_t('master') }} </span>:
                            </v-list-item-title>
                            <v-list-item-subtitle class="text-right">
                                <cluster-server-tooltip
                                    v-if="$typy(cluster, 'children[0]').isDefined"
                                    :servers="[$typy(cluster, 'children[0]').safeObject]"
                                >
                                    <template v-slot:activator="{ on }">
                                        <div class="d-inline-flex" v-on="on">
                                            <icon-sprite-sheet
                                                size="16"
                                                class="server-state-icon mr-1"
                                                :frame="
                                                    $helpers.serverStateIcon(
                                                        $typy(
                                                            cluster,
                                                            'children[0].serverData.attributes.state'
                                                        ).safeString
                                                    )
                                                "
                                            >
                                                servers
                                            </icon-sprite-sheet>
                                            <mxs-truncate-str
                                                :tooltipItem="{
                                                    txt: `${cluster.children[0].name}`,
                                                    activatorID: $helpers.lodash.uniqueId(
                                                        'clusters__cluster-children-name'
                                                    ),
                                                }"
                                            />
                                        </div>
                                    </template>
                                </cluster-server-tooltip>
                            </v-list-item-subtitle>
                        </v-list-item>
                        <v-list-item>
                            <v-list-item-title
                                class="text-subtitle-2 mxs-color-helper text-navigation"
                            >
                                <span class="text-uppercase">{{ $mxs_tc('slaves', 2) }} </span>:
                            </v-list-item-title>
                            <v-list-item-subtitle class="text-right">
                                <v-chip
                                    v-for="(item, stateType) in groupSlaveServersByStateType(
                                        cluster
                                    )"
                                    :key="stateType"
                                    :color="stateType"
                                    text-color="white"
                                    class="ml-1 lighten-1 d-inline-block"
                                    small
                                >
                                    <cluster-server-tooltip :servers="item.servers">
                                        <template v-slot:activator="{ on }">
                                            <div v-on="on">
                                                <v-avatar
                                                    style="margin-left: -12px; border-radius: 50% 0 0;"
                                                    :class="`mxs-color-helper bg-${stateType}`"
                                                    left
                                                >
                                                    <strong>{{ item.servers.length }}</strong>
                                                </v-avatar>
                                                <span class="text-lowercase">
                                                    {{ $mxs_t(item.label) }}
                                                </span>
                                            </div>
                                        </template>
                                    </cluster-server-tooltip>
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions, mapMutations } from 'vuex'
import PageHeaderRight from './PageHeaderRight'
import ClusterServerTooltip from './ClusterServerTooltip'
export default {
    name: 'clusters',
    components: {
        'page-header-right': PageHeaderRight,
        'cluster-server-tooltip': ClusterServerTooltip,
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
            SET_CURRENT_CLUSTER: 'visualization/SET_CURRENT_CLUSTER',
        }),
        navToCluster(cluster) {
            this.$router.push({
                path: `/visualization/clusters/${cluster.id}`,
            })
            this.SET_CURRENT_CLUSTER(cluster)
        },
        /**
         * Group servers with the same states together.
         * The state type is determined by using $helpers.serverStateIcon
         * @param {Object} cluster
         * @return {Object}
         */
        groupSlaveServersByStateType(cluster) {
            let group = {}
            const master = this.$typy(cluster, 'children[0]').safeObject
            if (master) {
                this.$helpers.flattenTree(this.$typy(master, 'children').safeArray).forEach(n => {
                    const groupName = this.getServerStateType(n.serverData.attributes.state)
                    const label = this.labellingStateType(groupName)
                    if (!group[groupName]) group[groupName] = { label, servers: [] }
                    group[groupName].servers.push(n)
                })
            }
            return group
        },
        getServerStateType(state) {
            switch (this.$helpers.serverStateIcon(state)) {
                case 0:
                    return 'error'
                case 1:
                    return 'success'
                case 2:
                    return 'grayed-out' // mxs-color-helper for maintenance state
            }
        },
        labellingStateType(stateType) {
            switch (stateType) {
                case 'error':
                    return 'down'
                case 'success':
                    return 'up'
                // grayed-out when server is in maintenance state
                case 'grayed-out':
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
