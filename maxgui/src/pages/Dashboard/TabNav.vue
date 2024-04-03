<template>
    <v-tabs v-model="activeTab" class="v-tabs--mariadb">
        <v-tab v-for="route in dashboardTabRoutes" :key="route.path" :to="route.path">
            {{ $mxs_tc(route.text, 2) }}
            <span class="grayed-out-info"> ({{ getTotal(route.name) }}) </span>
        </v-tab>
        <v-tabs-items v-model="activeTab">
            <v-tab-item
                v-for="route in dashboardTabRoutes"
                :id="route.path"
                :key="route.name"
                class="pt-2"
            >
                <!-- Only render view if tab is active -->
                <router-view v-if="activeTab === route.path" />
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'
import { dashboardTabRoutes } from '@src/router/routes'

export default {
    name: 'tab-nav',

    data() {
        return {
            activeTab: null,
            dashboardTabRoutes: dashboardTabRoutes,
        }
    },
    computed: {
        ...mapGetters({
            getTotalFilters: 'filter/getTotalFilters',
            getTotalListeners: 'listener/getTotalListeners',
            getTotalServers: 'server/getTotalServers',
            getTotalServices: 'service/getTotalServices',
            getTotalSessions: 'session/getTotalSessions',
        }),
    },
    methods: {
        getTotal(name) {
            return this[`getTotal${this.$helpers.capitalizeFirstLetter(name)}`]
        },
    },
}
</script>
