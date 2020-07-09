<template>
    <data-table
        :search="searchKeyWord"
        :headers="tableHeaders"
        :data="generateTableRows"
        :sortDesc="false"
        sortBy="id"
    >
        <!-- <template v-slot:user="{ data: { item: { user } } }">
            <router-link :key="user" :to="`/users/${user}`" class="no-underline">
                <span> {{ user }} </span>
            </router-link>
        </template> -->
        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span v-if="typeof serviceIds === 'string'">{{ serviceIds }}</span>

            <!--
                https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/REST-API/Resources-Session.md
                Each session is created on a service, so even serviceIds is an array, it always has one element
            -->
            <template v-else>
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
        </template>
        <template v-slot:connected="{ data: { item: { connected } } }">
            <span> {{ $help.formatValue(connected) }} </span>
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
    name: 'sessions',

    data() {
        return {
            tableHeaders: [
                { text: 'ID', value: 'id' },
                { text: 'Client', value: 'user' },
                { text: 'Connected', value: 'connected' },
                { text: 'IDLE (s)', value: 'idle' },
                { text: 'Service', value: 'serviceIds' },
            ],
        }
    },
    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            allSessions: 'session/allSessions',
            allServices: 'service/allServices',
        }),

        generateTableRows: function() {
            if (this.allSessions && this.allServices) {
                let itemsArr = []

                let allSessions = this.$help.lodash.cloneDeep(this.allSessions)
                for (let n = allSessions.length - 1; n >= 0; --n) {
                    /**
                     * @typedef {Object} row
                     * @property {Number} row.id - sessions's id
                     * @property {String} row.user - sessions's user
                     * @property {String} row.connected - sessions's sessions
                     * @property {Number} row.idle - idle (seconds)
                     * @property {Array} row.serviceIds - Sessions's services relationships
                     */
                    const {
                        id,
                        attributes: { idle, connected, user, remote },
                        relationships: { services: { data: allServices = [] } = {} },
                    } = allSessions[n] || {}

                    let serviceIds = allServices.length
                        ? allServices.map(item => `${item.id}`)
                        : this.$t('noEntity', { entityName: 'services' })

                    let row = {
                        id: id,
                        user: `${user}@${remote}`,
                        connected: connected,
                        idle: idle,
                        serviceIds: serviceIds,
                    }
                    itemsArr.push(row)
                }
                return itemsArr
            }
            return []
        },
    },

    methods: {
        serviceStateIcon(monitorState) {
            if (monitorState.includes('Started')) return 2
            if (monitorState.includes('Stopped')) return 0
            else return ''
        },
    },
}
</script>
