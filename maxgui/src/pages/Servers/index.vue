<template>
    <data-table
        :headers="tableHeaders"
        :data="dataProcessing"
        :colsHasRowSpan="2"
        :search="searchKeyWord"
        sortBy="groupId"
    >
        <template v-slot:header-append-groupId>
            <span class="ml-1 color text-field-text"> ({{ allLinkedMonitors }}) </span>
        </template>
        <template v-slot:header-append-id>
            <span class="ml-1 color text-field-text"> ({{ allServers.length }}) </span>
        </template>
        <template v-slot:header-append-serviceIds>
            <span class="ml-1 color text-field-text"> ({{ allLinkedServices }}) </span>
        </template>

        <template v-slot:groupId="{ data: { item: { groupId } } }">
            <router-link
                v-if="groupId !== $t('not', { action: 'monitored' })"
                :to="`/dashboard/monitors/${groupId}`"
                class="no-underline"
            >
                <span class="font-weight-bold">{{ groupId }} </span>
            </router-link>
            <span v-else>{{ groupId }} </span>
        </template>

        <template v-slot:monitorState="{ data: { item: { monitorState } } }">
            <div class="d-flex align-center">
                <icon-sprite-sheet
                    v-if="monitorState"
                    size="13"
                    class="status-icon mr-1"
                    :frame="$help.monitorStateIcon(monitorState)"
                >
                    status
                </icon-sprite-sheet>
                <span>{{ monitorState }} </span>
            </div>
        </template>

        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :to="`/dashboard/servers/${id}`" class="no-underline">
                <span>{{ id }} </span>
            </router-link>
        </template>

        <template v-slot:serverState="{ data: { item: { serverState } } }">
            <div class="d-flex align-center">
                <icon-sprite-sheet
                    size="13"
                    class="mr-1 status-icon"
                    :frame="$help.serverStateIcon(serverState)"
                >
                    status
                </icon-sprite-sheet>
                <span>{{ serverState }}</span>
            </div>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span v-if="typeof serviceIds === 'string'">{{ serviceIds }} </span>

            <template v-else-if="serviceIds.length < 2">
                <template v-for="serviceId in serviceIds">
                    <router-link
                        :key="serviceId"
                        :to="`/dashboard/services/${serviceId}`"
                        class="no-underline"
                    >
                        <span>{{ serviceId }} </span>
                    </router-link>
                </template>
            </template>

            <v-menu
                v-else
                offset-x
                transition="slide-x-transition"
                :close-on-content-click="false"
                open-on-hover
                nudge-right="20"
                nudge-top="12.5"
                content-class="shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <span class="pointer color text-links" v-on="on">
                        {{ serviceIds.length }}
                        {{ $tc('services', 2).toLowerCase() }}
                    </span>
                </template>

                <v-sheet style="border-radius: 10px;" class="pa-4">
                    <template v-for="serviceId in serviceIds">
                        <router-link
                            :key="serviceId"
                            :to="`/dashboard/services/${serviceId}`"
                            class="body-2 d-block no-underline"
                        >
                            <span>{{ serviceId }} </span>
                        </router-link>
                    </template>
                </v-sheet>
            </v-menu>
        </template>
    </data-table>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'

export default {
    data() {
        return {
            tableHeaders: [
                { text: `Monitor`, value: 'groupId' },
                { text: 'State', value: 'monitorState' },
                { text: 'Servers', value: 'id' },
                { text: 'Address', value: 'serverAddress' },
                { text: 'Port', value: 'serverPort' },
                { text: 'Connections', value: 'serverConnections' },
                { text: 'State', value: 'serverState' },
                { text: 'GTID', value: 'gtid' },
                { text: 'Services', value: 'serviceIds' },
            ],
            allLinkedServices: 0,
            allLinkedMonitors: 0,
        }
    },
    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            allMonitorsMap: 'monitor/allMonitorsMap',
            allMonitors: 'monitor/allMonitors',
            allServers: 'server/allServers',
        }),
        dataProcessing: function() {
            if (this.allServers.length && this.allMonitorsMap.size) {
                let tableRows = []
                let allServers = this.$help.lodash.cloneDeep(this.allServers)
                let totalServices = []
                let totalMonitors = []
                for (let index = 0; index < allServers.length; ++index) {
                    const {
                        id,
                        attributes: {
                            state: serverState,
                            parameters,
                            statistics,
                            gtid_current_pos,
                        },
                        relationships: {
                            services: { data: allServices = [] } = {},
                            monitors: { data: linkedMonitors = [] } = {},
                        },
                    } = allServers[index]

                    let serviceIds = allServices.length
                        ? allServices.map(item => `${item.id}`)
                        : this.$t('noEntity', { entityName: 'services' })
                    // get total number of unique services
                    if (typeof serviceIds !== 'string')
                        totalServices = [...totalServices, ...serviceIds]

                    let uniqueSet = new Set(totalServices)
                    this.setTotalNumOfLinkedServices([...uniqueSet].length)

                    let row = {
                        id: id,
                        serverAddress: parameters.address,
                        serverPort: parameters.port,
                        serverConnections: statistics.connections,
                        serverState: serverState,
                        serviceIds: serviceIds,
                        gtid: gtid_current_pos,
                    }
                    if (linkedMonitors.length) {
                        // The linkedMonitors is always an array with one element -> get monitor at index 0
                        let monitorLinked = this.allMonitorsMap.get(linkedMonitors[0].id)
                        if (!this.$help.lodash.isEmpty(monitorLinked)) {
                            totalMonitors.push(monitorLinked)
                            row.groupId = monitorLinked.id // aka monitorId
                            row.monitorState = `${monitorLinked.attributes.state}`
                        }
                    } else {
                        row.groupId = this.$t('not', { action: 'monitored' })
                        row.monitorState = ''
                    }
                    tableRows.push(row)
                }
                // set total number of monitors
                let uniqueMonitorSet = new Set(totalMonitors)
                this.setTotalNumOfLinkedMonitors([...uniqueMonitorSet].length)

                return tableRows
            }

            return []
        },
    },

    methods: {
        setTotalNumOfLinkedServices(total) {
            this.allLinkedServices = total
        },
        setTotalNumOfLinkedMonitors(total) {
            this.allLinkedMonitors = total
        },
    },
}
</script>
