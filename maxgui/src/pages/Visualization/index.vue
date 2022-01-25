<template>
    <page-wrapper class="d-flex flex-column">
        <v-sheet class="pt-2 fill-height d-flex flex-column">
            <page-header />
            <v-tabs
                v-model="activeTab"
                class="tab-navigation-wrapper d-flex flex-column fill-height"
            >
                <v-tab v-for="route in visRoutes" :key="route.path" :to="route.path">
                    {{ $t(route.name) }}
                </v-tab>

                <v-tabs-items v-model="activeTab">
                    <v-tab-item
                        v-for="route in visRoutes"
                        :id="route.path"
                        :key="route.name"
                        class="pt-5"
                    >
                        <div class="fill-height">
                            <router-view />
                        </div>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-sheet>
    </page-wrapper>
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
import PageHeader from './PageHeader'
import visRoutes from 'router/visRoutes'

export default {
    name: 'visualization',
    components: {
        PageHeader,
    },
    data() {
        return {
            activeTab: null,
        }
    },
    computed: {
        visRoutes() {
            return visRoutes
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .tab-navigation-wrapper {
    .v-window,
    .v-window-item {
        height: 100%;
    }
}
</style>
