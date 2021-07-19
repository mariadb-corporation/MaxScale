<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :key="id" :to="`/dashboard/services/${id}`" class="no-underline">
                <span> {{ id }}</span>
            </router-link>
        </template>
        <template v-slot:state="{ data: { item: { state } } }">
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.serviceStateIcon(state)"
            >
                status
            </icon-sprite-sheet>
            <span>{{ state }} </span>
        </template>

        <template v-slot:header-append-serverIds>
            <span class="ml-1 color text-field-text"> ({{ serversLength }}) </span>
        </template>
        <template v-slot:serverIds="{ data: { item: { serverIds }, i } }">
            <span v-if="typeof serverIds === 'string'">{{ serverIds }} </span>

            <template v-else-if="serverIds.length < 3">
                <template v-for="(serverId, i) in serverIds">
                    <router-link
                        :key="serverId"
                        :to="`/dashboard/servers/${serverId}`"
                        class="no-underline"
                    >
                        <span> {{ serverId }}{{ i !== serverIds.length - 1 ? ', ' : '' }} </span>
                    </router-link>
                </template>
            </template>

            <v-menu
                v-else
                :key="i"
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
                        {{ serverIds.length }}
                        {{ $tc('servers', 2).toLowerCase() }}
                    </span>
                </template>

                <v-sheet style="border-radius: 10px;" class="pa-4">
                    <template v-for="serverId in serverIds">
                        <router-link
                            :key="serverId"
                            :to="`/dashboard/servers/${serverId}`"
                            class="body-2 d-block no-underline"
                        >
                            <span>{{ serverId }} </span>
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
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'services',

    data() {
        return {
            tableHeaders: [
                { text: 'Service', value: 'id' },
                { text: 'State', value: 'state' },
                { text: 'Router', value: 'router' },
                { text: 'Current Sessions', value: 'connections' },
                { text: 'Total Sessions', value: 'total_connections' },
                { text: 'Servers', value: 'serverIds' },
            ],
            serversLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_services: state => state.service.all_services,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows: function() {
            let rows = []
            let allServerIds = []
            this.all_services.forEach(services => {
                const {
                    id,
                    attributes: { state, router, connections, total_connections },
                    relationships: { servers: { data: associatedServers = [] } = {} },
                } = services || {}

                const serverIds = associatedServers.length
                    ? associatedServers.map(item => `${item.id}`)
                    : this.$t('noEntity', { entityName: 'servers' })

                if (typeof serverIds !== 'string') allServerIds = [...allServerIds, ...serverIds]

                const row = {
                    id: id,
                    state: state,
                    router: router,
                    connections: connections,
                    total_connections: total_connections,
                    serverIds: serverIds,
                }
                rows.push(row)
            })
            const uniqueServerId = new Set(allServerIds) // get unique servers
            this.setServersLength([...uniqueServerId].length)
            return rows
        },
    },
    methods: {
        setServersLength(total) {
            this.serversLength = total
        },
    },
}
</script>
