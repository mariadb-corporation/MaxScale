<template>
    <data-table
        :headers="tableHeaders"
        :data="tableRows"
        :colsHasRowSpan="2"
        :search="search_keyword"
        sortBy="groupId"
        :itemsPerPage="-1"
    >
        <template v-slot:header-append-groupId>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ monitorsLength }}) </span>
        </template>
        <template v-slot:header-append-id>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ all_servers.length }}) </span>
        </template>
        <template v-slot:header-append-serviceIds>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ servicesLength }}) </span>
        </template>

        <template v-slot:groupId="{ data: { item: { groupId } } }">
            <router-link
                v-if="groupId !== $mxs_t('not', { action: 'monitored' })"
                v-mxs-highlighter="{ keyword: search_keyword, txt: groupId }"
                :to="`/dashboard/monitors/${groupId}`"
                class="rsrc-link font-weight-bold"
            >
                {{ groupId }}
            </router-link>
            <span v-else v-mxs-highlighter="{ keyword: search_keyword, txt: groupId }">
                {{ groupId }}
            </span>
        </template>
        <template v-slot:groupId-append="{ data: { item: { groupId } } }">
            <span
                v-if="isCooperative(groupId)"
                class="ml-1 mxs-color-helper text-success cooperative-indicator"
            >
                Primary
            </span>
        </template>
        <template v-slot:monitorState="{ data: { item: { monitorState } } }">
            <div class="d-flex align-center">
                <status-icon
                    v-if="monitorState"
                    size="16"
                    class="monitor-state-icon mr-1"
                    :type="MXS_OBJ_TYPES.MONITORS"
                    :value="monitorState"
                />
                <span v-mxs-highlighter="{ keyword: search_keyword, txt: monitorState }">
                    {{ monitorState }}
                </span>
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
                :disabled="!(isSlave || isMaster)"
                :serverInfo="serverInfo"
                :isMaster="isMaster"
                :open-delay="400"
                :top="true"
            >
                <template v-slot:activator="{ on }">
                    <div
                        :class="{
                            'override-td--padding disable-auto-truncate pointer text-truncate':
                                isSlave || isMaster,
                        }"
                        v-on="on"
                    >
                        <router-link
                            v-mxs-highlighter="{ keyword: search_keyword, txt: id }"
                            :to="`/dashboard/servers/${id}`"
                            class="rsrc-link"
                        >
                            {{ id }}
                        </router-link>
                    </div>
                </template>
            </rep-tooltip>
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
                        :class="{ pointer: isSlave || isMaster }"
                        v-on="on"
                    >
                        <status-icon
                            size="16"
                            class="mr-1 server-state-icon"
                            :type="MXS_OBJ_TYPES.SERVERS"
                            :value="serverState"
                        />
                        <span v-mxs-highlighter="{ keyword: search_keyword, txt: serverState }">
                            {{ serverState }}
                        </span>
                    </div>
                </template>
            </rep-tooltip>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span
                v-if="typeof serviceIds === 'string'"
                v-mxs-highlighter="{ keyword: search_keyword, txt: serviceIds }"
            >
                {{ serviceIds }}
            </span>

            <template v-else-if="serviceIds.length === 1">
                <router-link
                    v-mxs-highlighter="{ keyword: search_keyword, txt: serviceIds[0] }"
                    :to="`/dashboard/services/${serviceIds[0]}`"
                    class="rsrc-link"
                >
                    {{ serviceIds[0] }}
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
                        class="pointer mxs-color-helper text-anchor override-td--padding disable-auto-truncate"
                        v-on="on"
                    >
                        {{ serviceIds.length }}
                        {{ $mxs_tc('services', 2).toLowerCase() }}
                    </div>
                </template>

                <v-sheet class="pa-4">
                    <router-link
                        v-for="(serviceId, i) in serviceIds"
                        :key="i"
                        v-mxs-highlighter="{ keyword: search_keyword, txt: serviceId }"
                        :to="`/dashboard/services/${serviceId}`"
                        class="text-body-2 d-block rsrc-link"
                    >
                        {{ serviceId }}
                    </router-link>
                </v-sheet>
            </v-menu>
        </template>
    </data-table>
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
import { mapGetters, mapState } from 'vuex'
import { MRDB_MON } from '@rootSrc/constants'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    data() {
        return {
            tableHeaders: [
                {
                    text: `Monitor`,
                    value: 'groupId',
                    autoTruncate: true,
                    padding: '0px 0px 0px 24px',
                },
                { text: 'State', value: 'monitorState', padding: '0px 12px 0px 24px' },
                { text: 'Servers', value: 'id', autoTruncate: true, padding: '0px 0px 0px 24px' },
                { text: 'Address', value: 'serverAddress', padding: '0px 0px 0px 24px' },
                {
                    text: 'Connections',
                    value: 'serverConnections',
                    autoTruncate: true,
                    padding: '0px 0px 0px 24px',
                },
                { text: 'State', value: 'serverState', padding: '0px 0px 0px 24px' },
                { text: 'GTID', value: 'gtid', padding: '0px 0px 0px 24px' },
                { text: 'Services', value: 'serviceIds', autoTruncate: true },
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
            getAllServersMap: 'server/getAllServersMap',
        }),
        tableRows: function() {
            let rows = []
            if (this.all_servers.length) {
                let allServiceIds = []
                let allMonitorIds = []
                let allMonitorsMapClone = this.$helpers.lodash.cloneDeep(this.getAllMonitorsMap)
                this.all_servers.forEach(server => {
                    const {
                        id,
                        attributes: {
                            state: serverState,
                            parameters: { address, port, socket },
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
                        : this.$mxs_t('noEntity', { entityName: 'services' })

                    if (typeof serviceIds !== 'string')
                        allServiceIds = [...allServiceIds, ...serviceIds]

                    let row = {
                        id,
                        serverAddress: socket ? socket : `${address}:${port}`,
                        serverConnections,
                        serverState,
                        serviceIds,
                        gtid,
                    }

                    if (this.getAllMonitorsMap.size && monitorsData.length) {
                        // The monitorsData is always an array with one element -> get monitor at index 0
                        const {
                            id: monitorId = null,
                            attributes: {
                                state: monitorState,
                                module: monitorModule,
                                monitor_diagnostics: { master: masterName, server_info = [] } = {},
                            } = {},
                        } = this.getAllMonitorsMap.get(monitorsData[0].id) || {}

                        if (monitorId) {
                            allMonitorIds.push(monitorId)
                            row.groupId = monitorId
                            row.monitorState = monitorState
                            if (monitorModule === MRDB_MON) {
                                if (masterName === row.id) {
                                    row.isMaster = true
                                    row.serverInfo = server_info.filter(
                                        server => server.name !== masterName
                                    )
                                } else {
                                    row.isSlave = true
                                    row.serverInfo = server_info.filter(
                                        server => server.name === row.id
                                    )
                                }
                            }
                            // delete monitor that already grouped from allMonitorsMapClone
                            allMonitorsMapClone.delete(monitorId)
                        }
                    } else {
                        row.groupId = this.$mxs_t('not', { action: 'monitored' })
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
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
        setMonitorsLength(total) {
            this.monitorsLength = total
        },
        isCooperative(id) {
            return this.$typy(
                this.getAllMonitorsMap.get(id),
                'attributes.monitor_diagnostics.primary'
            ).safeBoolean
        },
    },
}
</script>

<style lang="scss" scoped>
.cooperative-indicator {
    font-size: 0.75rem;
}
</style>
