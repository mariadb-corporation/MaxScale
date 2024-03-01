<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
        :itemsPerPage="-1"
        :customFilter="customFilter"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :key="id" :to="`/dashboard/services/${id}`" class="rsrc-link">
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

        <template v-slot:header-append-routingTargets>
            <span class="ml-1 color text-field-text"> ({{ routingTargetsLength }}) </span>
        </template>
        <template v-slot:routingTargets="{ data: { item: { routingTargets } } }">
            <span v-if="typeof routingTargets === 'string'">{{ routingTargets }} </span>

            <template v-else-if="routingTargets.length < 3">
                <router-link
                    v-for="(target, i) in routingTargets"
                    :key="target.id"
                    :to="`/dashboard/${target.type}/${target.id}`"
                    class="rsrc-link"
                >
                    <span> {{ target.id }}{{ i !== routingTargets.length - 1 ? ', ' : '' }} </span>
                </router-link>
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
                        class="pointer color text-links override-td--padding disable-auto-truncate"
                        v-on="on"
                    >
                        {{ routingTargets.length }}
                        targets
                    </div>
                </template>

                <v-sheet class="pa-4">
                    <router-link
                        v-for="target in routingTargets"
                        :key="target.id"
                        :to="`/dashboard/${target.type}/${target.id}`"
                        class="text-body-2 d-block rsrc-link"
                    >
                        <span> {{ target.id }} </span>
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
 * Change Date: 2028-02-27
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
                { text: 'Service', value: 'id', autoTruncate: true },
                { text: 'State', value: 'state' },
                { text: 'Router', value: 'router' },
                { text: 'Current Sessions', value: 'connections', autoTruncate: true },
                { text: 'Total Sessions', value: 'total_connections', autoTruncate: true },
                { text: this.$t('routingTargets'), value: 'routingTargets', autoTruncate: true },
            ],
            routingTargetsLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_services: state => state.service.all_services,
            ROUTING_TARGET_RELATIONSHIP_TYPES: state =>
                state.app_config.ROUTING_TARGET_RELATIONSHIP_TYPES,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows() {
            let rows = []
            let allRoutingTargets = []
            this.all_services.forEach(service => {
                const {
                    id,
                    attributes: { state, router, connections, total_connections },
                    relationships = {},
                } = service || {}

                const targets = Object.keys(relationships).reduce((arr, type) => {
                    if (this.ROUTING_TARGET_RELATIONSHIP_TYPES.includes(type)) {
                        arr = [...arr, ...this.$typy(relationships[type], 'data').safeArray]
                    }
                    return arr
                }, [])
                const routingTargets = targets.length
                    ? targets
                    : this.$t('noEntity', { entityName: this.$t('routingTargets') })

                if (typeof routingTargets !== 'string')
                    allRoutingTargets = [...allRoutingTargets, ...routingTargets]

                const row = { id, state, router, connections, total_connections, routingTargets }
                rows.push(row)
            })
            const uniqueRoutingTargetIds = new Set(allRoutingTargets.map(target => target.id))
            this.setRoutingTargetsLength([...uniqueRoutingTargetIds].length)
            return rows
        },
    },
    methods: {
        setRoutingTargetsLength(total) {
            this.routingTargetsLength = total
        },
        customFilter(v, search) {
            let value = `${v}`
            // filter for routingTargets
            if (this.$typy(v).isArray) value = v.map(v => v.id).join(', ')
            return this.$help.ciStrIncludes(value, search)
        },
    },
}
</script>
