<template>
    <page-wrapper class="d-flex flex-column">
        <v-sheet class="pt-2 fill-height d-flex flex-column">
            <portal to="page-header">
                <div class="d-flex align-center">
                    <div class="d-inline-flex align-center">
                        <h4
                            style="line-height: normal;"
                            class="ml-1 mb-0 color text-navigation text-h4 text-capitalize"
                        >
                            {{ $t($route.name) }}
                        </h4>
                    </div>
                </div>
            </portal>
            <v-tabs v-model="activeTab" class="tab-navigation-wrapper">
                <v-tab v-for="route in visRoutes" :key="route.path" :to="route.path">
                    {{ $t(route.name) }}
                </v-tab>
            </v-tabs>
            <div class="pt-5 fill-height">
                <router-view />
            </div>
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
import visRoutes from 'router/visRoutes'

export default {
    name: 'visualization',
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
