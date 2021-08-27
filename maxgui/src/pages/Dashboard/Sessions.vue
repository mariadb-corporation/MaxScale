<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="true"
        sortBy="connected"
    >
        <template v-slot:header-append-serviceIds>
            <span class="ml-1 color text-field-text"> ({{ servicesLength }}) </span>
        </template>
        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span v-if="typeof serviceIds === 'string'">{{ serviceIds }}</span>
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
            <span> {{ $help.dateFormat({ value: connected }) }} </span>
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
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

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
            servicesLength: 0,
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_sessions: state => state.session.all_sessions,
        }),

        tableRows: function() {
            let rows = []
            let allServiceNames = []
            this.all_sessions.forEach(session => {
                const {
                    id,
                    attributes: { idle, connected, user, remote },
                    relationships: { services: { data: associatedServices = [] } = {} },
                } = session || {}

                const serviceIds = associatedServices.length
                    ? associatedServices.map(item => `${item.id}`)
                    : this.$t('noEntity', { entityName: 'services' })

                if (typeof serviceIds !== 'string')
                    allServiceNames = [...allServiceNames, ...serviceIds]

                rows.push({
                    id: id,
                    user: `${user}@${remote}`,
                    connected: connected,
                    idle: idle,
                    serviceIds: serviceIds,
                })
            })
            const uniqueServiceId = new Set(allServiceNames) // get unique service names
            this.setServicesLength([...uniqueServiceId].length)

            return rows
        },
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
    },
}
</script>
