<template>
    <data-table
        :search="searchKeyWord"
        :headers="tableHeaders"
        :data="generateTableRows"
        :sortDesc="false"
        sortBy="id"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :key="id" :to="`/dashboard/services/${id}`" class="no-underline">
                <span> {{ id }}</span>
            </router-link>
        </template>
        <template v-slot:state="{ data: { item: { state } } }">
            <icon-sprite-sheet size="13" class="status-icon" :frame="$help.serviceStateIcon(state)">
                status
            </icon-sprite-sheet>
        </template>

        <template v-slot:header-append-serverIds>
            <span class="ml-1 color text-field-text"> ({{ allLinkedServers }}) </span>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'

export default {
    name: 'services',

    data() {
        return {
            tableHeaders: [
                { text: 'Service', value: 'id' },
                { text: 'Status', value: 'state', align: 'center' },
                { text: 'Router', value: 'router' },
                { text: 'Current Sessions', value: 'connections' },
                { text: 'Total Sessions', value: 'total_connections' },
                { text: 'Servers', value: 'serverIds' },
            ],
            allLinkedServers: 0,
        }
    },

    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            allServices: 'service/allServices',
        }),

        /**
         * @return {Array} An array of objects
         */
        generateTableRows: function() {
            /**
             * @param {Array} itemsArr
             *  Elements are {Object} row
             */
            if (this.allServices) {
                let itemsArr = []
                const { allServices } = this
                let totalUniqueServers = []
                for (let n = allServices.length - 1; n >= 0; --n) {
                    /**
                     * @typedef {Object} row
                     * @property {String} row.id - Service's name
                     * @property {Array} row.state - Server's state
                     * @property {String} row.router - Server's router
                     * @property {Number} row.connections - Number of connections to the service
                     * @property {Number} row.total_connections - Total number of connections to the service
                     * @property {Array} row.serverIds - List of servers use this service
                     */
                    const {
                        id,
                        attributes: { state, router, connections, total_connections },
                        relationships: { servers: { data: allServers = [] } = {} },
                    } = allServices[n] || {}

                    let serverIds = allServers.length
                        ? allServers.map(item => `${item.id}`)
                        : this.$t('noEntity', { entityName: 'servers' })

                    // get total number of unique servers
                    if (typeof serverIds !== 'string')
                        totalUniqueServers = [...totalUniqueServers, ...serverIds]

                    let uniqueServerSet = new Set(totalUniqueServers)
                    this.setTotalNumOfLinkedServers([...uniqueServerSet].length)

                    let row = {
                        id: id,
                        state: state,
                        router: router,
                        connections: connections,
                        total_connections: total_connections,
                        serverIds: serverIds,
                    }
                    itemsArr.push(row)
                }
                return itemsArr
            }
            return []
        },
    },
    methods: {
        setTotalNumOfLinkedServers(total) {
            this.allLinkedServers = total
        },
    },
}
</script>
