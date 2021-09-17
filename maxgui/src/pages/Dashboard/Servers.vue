<template>
    <div>
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

            <template
                v-slot:id="{
                    data: {
                        item: { id, showRepStats, showSlaveStats },
                    },
                }"
            >
                <rep-tooltip
                    v-if="showRepStats || showSlaveStats"
                    :slaveConnectionsMap="slaveConnectionsMap"
                    :slaveServersByMasterMap="slaveServersByMasterMap"
                    :showRepStats="showRepStats"
                    :showSlaveStats="showSlaveStats"
                    :serverId="id"
                    :openDelay="400"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="override-td--padding disable-auto-truncate"
                            :class="{
                                pointer: showRepStats || showSlaveStats,
                            }"
                            v-on="on"
                        >
                            <div class="text-truncate">
                                <router-link :to="`/dashboard/servers/${id}`" class="no-underline ">
                                    {{ id }}
                                </router-link>
                            </div>
                        </div>
                    </template>
                </rep-tooltip>

                <router-link v-else :to="`/dashboard/servers/${id}`" class="no-underline">
                    {{ id }}
                </router-link>
            </template>

            <template
                v-slot:serverState="{
                    data: {
                        item: { id, serverState, showRepStats, showSlaveStats },
                    },
                }"
            >
                <rep-tooltip
                    v-if="serverState"
                    :slaveConnectionsMap="slaveConnectionsMap"
                    :slaveServersByMasterMap="slaveServersByMasterMap"
                    :showRepStats="showRepStats"
                    :showSlaveStats="showSlaveStats"
                    :serverId="id"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="override-td--padding"
                            :class="{
                                pointer: showRepStats || showSlaveStats,
                            }"
                            v-on="on"
                        >
                            <icon-sprite-sheet
                                size="13"
                                class="mr-1 status-icon"
                                :frame="$help.serverStateIcon(serverState)"
                            >
                                status
                            </icon-sprite-sheet>
                            {{ serverState }}
                        </div>
                    </template>
                </rep-tooltip>
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
                    top
                    offset-y
                    transition="slide-y-transition"
                    :close-on-content-click="false"
                    open-on-hover
                    allow-overflow
                    content-class="shadow-drop"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="pointer color text-links override-td--padding disable-auto-truncate"
                            v-on="on"
                        >
                            {{ serviceIds.length }}
                            {{ $tc('services', 2).toLowerCase() }}
                        </div>
                    </template>

                    <v-sheet class="pa-4">
                        <template v-for="serviceId in serviceIds">
                            <router-link
                                :key="serviceId"
                                :to="`/dashboard/services/${serviceId}`"
                                class="text-body-2 d-block no-underline"
                            >
                                <span>{{ serviceId }} </span>
                            </router-link>
                        </template>
                    </v-sheet>
                </v-menu>
            </template>
        </data-table>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
import RepTooltip from './RepTooltip.vue'
export default {
    components: {
        'rep-tooltip': RepTooltip,
    },
    data() {
        return {
            tableHeaders: [
                { text: `Monitor`, value: 'groupId', autoTruncate: true },
                { text: 'State', value: 'monitorState' },
                { text: 'Servers', value: 'id', autoTruncate: true },
                { text: 'Address', value: 'serverAddress', autoTruncate: true },
                { text: 'Port', value: 'serverPort' },
                { text: 'Connections', value: 'serverConnections', autoTruncate: true },
                { text: 'State', value: 'serverState' },
                { text: 'GTID', value: 'gtid' },
                { text: 'Services', value: 'serviceIds', autoTruncate: true },
            ],
            servicesLength: 0,
            monitorsLength: 0,
            monitorSupportsReplica: 'mariadbmon',
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_servers: state => state.server.all_servers,
        }),
        ...mapGetters({
            getAllMonitorsMap: 'monitor/getAllMonitorsMap',
            getAllServersMap: 'server/getAllServersMap',
        }),
        tableRows: function() {
            let rows = []
            if (this.all_servers.length) {
                let allServiceIds = []
                let allMonitorIds = []
                let allMonitorsMapClone = this.$help.lodash.cloneDeep(this.getAllMonitorsMap)
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
                            services: { data: servicesData = [] } = {},
                            monitors: { data: monitorsData = [] } = {},
                        },
                    } = server

                    const serviceIds = servicesData.length
                        ? servicesData.map(item => `${item.id}`)
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
                    // show socket in address column if server is using socket
                    if (serverAddress === null && serverPort === null) row.serverAddress = socket

                    if (this.getAllMonitorsMap.size && monitorsData.length) {
                        // The monitorsData is always an array with one element -> get monitor at index 0
                        const {
                            id: monitorId = null,
                            attributes: {
                                state: monitorState,
                                module: monitorModule,
                                monitor_diagnostics: { master },
                            },
                        } = this.getAllMonitorsMap.get(monitorsData[0].id) || {}

                        if (monitorId) {
                            allMonitorIds.push(monitorId)
                            row.groupId = monitorId
                            row.monitorState = monitorState
                            row.showSlaveStats =
                                master === row.id && monitorModule === this.monitorSupportsReplica
                            row.showRepStats =
                                master !== row.id && monitorModule === this.monitorSupportsReplica
                            // delete monitor that already grouped from allMonitorsMapClone
                            allMonitorsMapClone.delete(monitorId)
                        }
                    } else {
                        row.groupId = this.$t('not', { action: 'monitored' })
                        row.monitorState = ''
                    }
                    rows.push(row)
                })

                // push monitors that don't monitor any servers to rows
                allMonitorsMapClone.forEach(monitor => {
                    allMonitorIds.push(monitor.id)
                    rows.push({
                        id: '',
                        serverAddress: '',
                        serverPort: '',
                        serverConnections: '',
                        serverState: '',
                        serviceIds: '',
                        gtid: '',
                        groupId: monitor.id,
                        monitorState: monitor.attributes.state,
                    })
                })

                const uniqueServiceId = new Set(allServiceIds) // get unique service ids
                this.setServicesLength([...uniqueServiceId].length)
                const uniqueMonitorId = new Set(allMonitorIds) // get unique monitor ids
                this.setMonitorsLength([...uniqueMonitorId].length)
            }
            return rows
        },
        slaveConnectionsMap() {
            let map = new Map()
            this.tableRows
                .filter(row => row.showRepStats)
                .forEach(row => {
                    const key = row.id
                    const server = this.getAllServersMap.get(row.id)
                    let slave_connections =
                        map.get(key) || this.$typy(server, 'attributes.slave_connections').safeArray
                    map.set(key, slave_connections)
                })
            return map
        },
        slaveServersByMasterMap() {
            let map = new Map()
            let group = this.$help.hashMapByPath({
                arr: this.tableRows.filter(row => row.groupId), // Group monitored servers
                path: 'groupId', //monitorId
            })
            Object.keys(group).forEach(key => {
                let master = null
                let slaves = []
                group[key].forEach(server => {
                    if (server.showSlaveStats) master = server.id
                    else slaves.push(server.id)
                })
                map.set(master, slaves)
            })
            return map
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
