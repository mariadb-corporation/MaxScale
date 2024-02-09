<template>
    <sessions-table
        :extraHeaders="[{ text: 'Service', value: 'serviceIds' }]"
        :items="tableRows"
        :server-items-length="getTotalSessions"
        @get-data-from-api="fetchSessions"
        @confirm-kill="killSession({ id: $event.id, callback: fetchSessions })"
    >
        <template v-slot:[`header.serviceIds`]="{ header }">
            {{ header.text }}
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ servicesLength }}) </span>
        </template>
        <template v-slot:[`item.serviceIds`]="{ value: serviceIds }">
            <span v-if="typeof serviceIds === 'string'">{{ serviceIds }}</span>
            <template v-else>
                <router-link
                    v-for="serviceId in serviceIds"
                    :key="serviceId"
                    :to="`/dashboard/services/${serviceId}`"
                    class="rsrc-link"
                >
                    {{ serviceId }}
                </router-link>
            </template>
        </template>
    </sessions-table>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapGetters, mapActions } from 'vuex'

export default {
    name: 'sessions',
    data() {
        return {
            servicesLength: 0,
        }
    },
    computed: {
        ...mapState({
            current_sessions: state => state.session.current_sessions,
        }),
        ...mapGetters({
            getTotalSessions: 'session/getTotalSessions',
            isAdmin: 'user/isAdmin',
        }),
        tableRows() {
            let rows = []
            let allServiceNames = []
            this.current_sessions.forEach(session => {
                const {
                    id,
                    attributes: { idle, connected, user, remote, memory, io_activity },
                    relationships: { services: { data: associatedServices = [] } = {} },
                } = session || {}

                const serviceIds = associatedServices.length
                    ? associatedServices.map(item => `${item.id}`)
                    : this.$mxs_t('noEntity', { entityName: 'services' })

                if (typeof serviceIds !== 'string')
                    allServiceNames = [...allServiceNames, ...serviceIds]

                rows.push({
                    id: id,
                    user: `${user}@${remote}`,
                    connected: this.$helpers.dateFormat({ value: connected }),
                    idle: idle,
                    memory,
                    io_activity,
                    serviceIds: serviceIds,
                })
            })
            const uniqueServiceId = new Set(allServiceNames) // get unique service names
            this.setServicesLength([...uniqueServiceId].length)

            return rows
        },
    },
    methods: {
        ...mapActions({
            killSession: 'session/killSession',
            fetchSessions: 'session/fetchSessions',
        }),
        setServicesLength(total) {
            this.servicesLength = total
        },
    },
}
</script>
