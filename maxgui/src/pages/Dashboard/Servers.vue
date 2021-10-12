<template>
    <data-table
        :headers="tableHeaders"
        :data="tableRows"
        :colsHasRowSpan="2"
        :search="search_keyword"
        sortBy="groupId"
    >
        <template v-slot:header-append-groupId>
            <span class="ml-1 color text-field-text"> ({{ monitorsLength }}) </span>
        </template>
        <template v-slot:header-append-id>
            <span class="ml-1 color text-field-text"> ({{ all_servers.length }}) </span>
        </template>
        <template v-slot:header-append-serviceIds>
            <span class="ml-1 color text-field-text"> ({{ servicesLength }}) </span>
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
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'

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
            servicesLength: 0,
            monitorsLength: 0,
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_servers: state => state.server.all_servers,
        }),
        ...mapGetters({
            getAllMonitorsMap: 'monitor/getAllMonitorsMap',
        }),
        tableRows: function() {
            let rows = []
            if (this.all_servers.length) {
                let allServiceIds = []
                let allMonitorIds = []

                this.all_servers.forEach(server => {
                    const {
                        id,
                        attributes: {
                            state: serverState,
                            parameters: { address: serverAddress, port: serverPort, socket },
                            statistics: { connections: serverConnections },
                            gtid_current_pos: gtid,
                        },
                        relationships: {
                            services: { data: associatedServices = [] } = {},
                            monitors: { data: associatedMonitors = [] } = {},
                        },
                    } = server

                    const serviceIds = associatedServices.length
                        ? associatedServices.map(item => `${item.id}`)
                        : this.$t('noEntity', { entityName: 'services' })

                    if (typeof serviceIds !== 'string')
                        allServiceIds = [...allServiceIds, ...serviceIds]

                    let row = {
                        id,
                        serverAddress,
                        serverPort,
                        serverConnections,
                        serverState,
                        serviceIds,
                        gtid,
                    }

                    if (serverAddress === null && serverPort === null) row.serverAddress = socket

                    if (this.getAllMonitorsMap.size && associatedMonitors.length) {
                        // The associatedMonitors is always an array with one element -> get monitor at index 0
                        const {
                            id: monitorId = null,
                            attributes: { state },
                        } = this.getAllMonitorsMap.get(associatedMonitors[0].id) || {}
                        if (monitorId) {
                            allMonitorIds.push(monitorId)
                            row.groupId = monitorId
                            row.monitorState = state
                        }
                    } else {
                        row.groupId = this.$t('not', { action: 'monitored' })
                        row.monitorState = ''
                    }
                    rows.push(row)
                })

                const uniqueServiceId = new Set(allServiceIds) // get unique service ids
                this.setServicesLength([...uniqueServiceId].length)
                const uniqueMonitorId = new Set(allMonitorIds) // get unique monitor ids
                this.setMonitorsLength([...uniqueMonitorId].length)
            }

            return rows
        },
    },

    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
        setMonitorsLength(total) {
            this.monitorsLength = total
        },
    },
}
</script>
