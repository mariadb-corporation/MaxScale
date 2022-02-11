<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :key="id" :to="`/dashboard/filters/${id}`" class="rsrc-link">
                <span> {{ id }}</span>
            </router-link>
        </template>

        <template v-slot:header-append-serviceIds>
            <span class="ml-1 color text-field-text"> ({{ servicesLength }}) </span>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span v-if="typeof serviceIds === 'string'">{{ serviceIds }} </span>

            <template v-else-if="serviceIds.length < 2">
                <template v-for="serviceId in serviceIds">
                    <router-link
                        :key="serviceId"
                        :to="`/dashboard/services/${serviceId}`"
                        class="rsrc-link"
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
                content-class="shadow-drop"
                :min-width="1"
            >
                <template v-slot:activator="{ on }">
                    <div
                        class="pointer color text-links  override-td--padding disable-auto-truncate"
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
                            class="text-body-2 d-block rsrc-link"
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
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'filters',

    data() {
        return {
            tableHeaders: [
                { text: 'Filter', value: 'id', autoTruncate: true },
                { text: 'Service', value: 'serviceIds', autoTruncate: true },
                { text: 'Module', value: 'module' },
            ],
            servicesLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_filters: state => state.filter.all_filters,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows: function() {
            let rows = []
            let allServiceIds = []
            this.all_filters.forEach(filter => {
                const {
                    id,
                    attributes: { module: filterModule } = {},
                    relationships: { services: { data: associatedServices = [] } = {} },
                } = filter || {}

                const serviceIds = associatedServices.length
                    ? associatedServices.map(item => `${item.id}`)
                    : this.$t('noEntity', { entityName: 'services' })

                if (typeof serviceIds !== 'string')
                    allServiceIds = [...allServiceIds, ...serviceIds]

                rows.push({
                    id: id,
                    serviceIds: serviceIds,
                    module: filterModule,
                })
            })
            const uniqueServiceId = new Set(allServiceIds) // get unique service ids
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
