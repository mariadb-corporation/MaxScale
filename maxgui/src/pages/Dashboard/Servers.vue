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

            <template v-slot:id="{ data: { item: { id } } }">
                <router-link :to="`/dashboard/servers/${id}`" class="no-underline">
                    <span>{{ id }} </span>
                </router-link>
            </template>

            <template
                v-slot:serverState="{
                    data: {
                        item: { id, serverState, showRepStats },
                        cellIndex,
                        rowIndex,
                    },
                }"
            >
                <div
                    class="d-inline py-3"
                    :class="{
                        [`pointer replicas-activator-row-${rowIndex}-cell-${cellIndex}`]: showRepStats,
                    }"
                    @mouseover="showRepStats ? onShowRepStatus({ id, cellIndex, rowIndex }) : null"
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
                        <div class="pointer color text-links" v-on="on">
                            {{ serviceIds.length }}
                            {{ $tc('services', 2).toLowerCase() }}
                        </div>
                    </template>

                    <v-sheet style="border-radius: 10px;" class="pa-4">
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
        <v-menu
            v-if="hoveredItem"
            top
            offset-y
            transition="slide-y-transition"
            :close-on-content-click="false"
            open-on-hover
            content-class="shadow-drop color text-navigation"
            allow-overflow
            eager
            :max-height="400"
            :activator="
                `.replicas-activator-row-${hoveredItem.rowIndex}-cell-${hoveredItem.cellIndex}`
            "
        >
            <v-sheet style="border-radius: 10px;" class="py-4 px-3 text-body-2">
                <div class="px-1 py-1 font-weight-bold ">
                    {{ $t('replicationStatus') }}
                    <!-- TODO: show icon for replicationStatus -->
                </div>
                <v-divider class="color border-separator" />
                <table class="px-1">
                    <template v-for="(srcReplication, i) in repStats">
                        <tbody :key="`${i}`" class="tbody-src-replication">
                            <tr v-for="(value, key) in srcReplication" :key="`${key}`">
                                <template>
                                    <td class="pr-5">
                                        {{ key }}
                                    </td>
                                    <td>
                                        <truncate-string
                                            wrap
                                            :text="`${value}`"
                                            :maxWidth="400"
                                            :nudgeTop="10"
                                        />
                                    </td>
                                </template>
                            </tr>
                        </tbody>
                    </template>
                </table>
            </v-sheet>
        </v-menu>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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
            hoveredItem: null,
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
        repStats() {
            return this.getRepStats(this.hoveredItem.id)
        },
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
        setMonitorsLength(total) {
            this.monitorsLength = total
        },
        onShowRepStatus(item) {
            this.hoveredItem = item
        },
        getRepStats(serverId) {
            const slave_connections = this.slaveConnectionsMap.get(serverId) || []
            if (!slave_connections.length) return []

            const repStats = []
            slave_connections.forEach(slave_conn => {
                const {
                    seconds_behind_master,
                    slave_io_running,
                    slave_sql_running,
                    last_io_error,
                    last_sql_error,
                    connection_name,
                } = slave_conn
                let srcRep = {}
                // show connection_name only when multi-source replication is in use
                if (slave_connections.length > 1) srcRep.connection_name = connection_name

                // Determine replication_state (Stopped||Running||Lagging)
                if (slave_io_running === 'No' || slave_sql_running === 'No')
                    srcRep.replication_state = 'Stopped'
                else if (seconds_behind_master === 0) {
                    if (slave_sql_running === 'Yes' && slave_io_running === 'Yes')
                        srcRep.replication_state = 'Running'
                    else {
                        // use value of either slave_io_running or slave_sql_running
                        srcRep.replication_state =
                            slave_io_running !== 'Yes' ? slave_io_running : slave_sql_running
                    }
                } else srcRep.replication_state = 'Lagging'
                // only show last_io_error and last_sql_error when replication_state === 'Stopped'
                if (srcRep.replication_state === 'Stopped')
                    srcRep = {
                        ...srcRep,
                        last_io_error,
                        last_sql_error,
                    }
                srcRep = {
                    ...srcRep,
                    seconds_behind_master,
                    slave_io_running,
                    slave_sql_running,
                }
                repStats.push(srcRep)
            })

            return repStats
        },
    },
}
</script>

<style lang="scss" scoped>
.tbody-src-replication:not(:last-child) {
    &::after {
        content: '';
        display: block;
        height: 10px;
    }
}
</style>
