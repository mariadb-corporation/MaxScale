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
                    class="rsrc-link"
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
                        item: { id, isSlave, isMaster,  serverInfo = [] },
                    },
                }"
            >
                <rep-tooltip
                    v-if="isSlave || isMaster"
                    :disabled="!(isSlave || isMaster)"
                    :serverInfo="serverInfo"
                    :isMaster="isMaster"
                    :open-delay="400"
                    :top="true"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="override-td--padding disable-auto-truncate"
                            :class="{
                                pointer: isSlave || isMaster,
                            }"
                            v-on="on"
                        >
                            <div class="text-truncate">
                                <router-link :to="`/dashboard/servers/${id}`" class="rsrc-link">
                                    {{ id }}
                                </router-link>
                            </div>
                        </div>
                    </template>
                </rep-tooltip>
                <router-link v-else :to="`/dashboard/servers/${id}`" class="rsrc-link">
                    {{ id }}
                </router-link>
            </template>

            <template
                v-slot:serverState="{
                    data: {
                        item: {  serverState, isSlave, isMaster, serverInfo = [] },
                    },
                }"
            >
                <rep-tooltip
                    v-if="serverState"
                    :disabled="!(isSlave || isMaster)"
                    :serverInfo="serverInfo"
                    :isMaster="isMaster"
                    :top="true"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            class="override-td--padding"
                            :class="{
                                pointer: isSlave || isMaster,
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
                    <router-link
                        v-for="(serviceId, i) in serviceIds"
                        :key="i"
                        :to="`/dashboard/services/${serviceId}`"
                        class="rsrc-link"
                    >
                        <span>{{ serviceId }} </span>
                    </router-link>
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
                        <router-link
                            v-for="(serviceId, i) in serviceIds"
                            :key="i"
                            :to="`/dashboard/services/${serviceId}`"
                            class="text-body-2 d-block rsrc-link"
                        >
                            <span>{{ serviceId }} </span>
                        </router-link>
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
 * Change Date: 2026-01-04
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
                                monitor_diagnostics: { master: masterName, server_info },
                            },
                        } = this.getAllMonitorsMap.get(monitorsData[0].id) || {}

                        if (monitorId) {
                            allMonitorIds.push(monitorId)
                            row.groupId = monitorId
                            row.monitorState = monitorState
                            if (monitorModule === this.monitorSupportsReplica) {
                                if (masterName === row.id) {
                                    row.isMaster = true
                                    row.serverInfo = this.getAllSlaveServersInfo({
                                        masterName,
                                        server_info,
                                    })
                                } else {
                                    row.isSlave = true
                                    // get info of the server has name equal to row.id
                                    row.serverInfo = this.getSlaveServerInfo({
                                        masterName,
                                        slaveName: row.id,
                                        server_info,
                                    })
                                }
                            }
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
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
        setMonitorsLength(total) {
            this.monitorsLength = total
        },
        /**
         * Keep only connections to master
         * @param {Array} param.slave_connections - slave_connections in monitor_diagnostics.server_info
         * @param {String} param.masterName - master server name
         * @returns {Array} returns connections that are connected to the provided masterName
         */
        filterSlaveConn({ slave_connections, masterName }) {
            return slave_connections.filter(conn => conn.master_server_name === masterName)
        },
        /**
         * Get info of the slave servers
         * @param {String} param.masterName - master server name
         * @param {Array} param.server_info - monitor_diagnostics.server_info
         * @returns {Array} returns all slave servers info of the provided masterName
         */
        getAllSlaveServersInfo({ masterName, server_info }) {
            return server_info.reduce((arr, item) => {
                if (item.name !== masterName)
                    arr.push({
                        ...item,
                        // Keep only connections to master
                        slave_connections: this.filterSlaveConn({
                            slave_connections: item.slave_connections,
                            masterName,
                        }),
                    })
                return arr
            }, [])
        },
        /**
         * Get info of the slave servers
         * @param {String} param.masterName - master server name
         * @param {String} param.slaveName - slave server name
         * @param {Array} param.server_info - monitor_diagnostics.server_info
         * @returns {Array} All slave servers info of the provided masterName
         */
        getSlaveServerInfo({ masterName, slaveName, server_info }) {
            return server_info.reduce((arr, item) => {
                if (item.name === slaveName)
                    arr.push({
                        ...item,
                        slave_connections: this.filterSlaveConn({
                            slave_connections: item.slave_connections,
                            masterName,
                        }),
                    })
                return arr
            }, [])
        },
    },
}
</script>
