<template>
    <v-navigation-drawer
        :mini-variant.sync="isMini"
        color="white"
        class="main-nav"
        width="210"
        mini-variant-width="50"
        fixed
        left
        clipped
        app
        permanent
        @mouseover.native="isMini = false"
        @mouseout.native="isMini = true"
    >
        <v-list>
            <v-list-item
                v-for="item in items"
                :key="item.label"
                class="my-2"
                @click="navigate(item)"
            >
                <div
                    class="nav-item d-flex align-center justify-center pa-2"
                    :class="{
                        'nav-item--active': currentPath.includes(item.path),
                    }"
                >
                    <v-icon
                        class="nav-item__icon"
                        :size="item.meta.size"
                        :color="currentPath.includes(item.path) ? 'blue-azure' : 'navigation'"
                    >
                        {{ item.meta.icon }}
                    </v-icon>
                    <span
                        v-show="!isMini"
                        class="nav-item__label ml-4 text-capitalize text-no-wrap"
                    >
                        {{
                            item.label === 'dashboards'
                                ? $mxs_tc(`${item.label}`, 1)
                                : $mxs_tc(`${item.label}`, 2)
                        }}
                    </span>
                </div>
            </v-list-item>
        </v-list>
    </v-navigation-drawer>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { sideBarRoutes } from '@rootSrc/router/routes'
export default {
    name: 'navigation',
    data() {
        return {
            isMini: true,
            items: sideBarRoutes,
        }
    },
    computed: {
        currentPath: function() {
            return this.$route.path
        },
    },
    methods: {
        navigate(nxtRoute) {
            const { path, meta } = nxtRoute
            /**
             * E.g. Sidebar dashboard path is /dashboard, but it'll be redirected to /dashboard/servers
             * This checks it and prevent redundant navigation
             */
            const isDupRoute = this.currentPath.includes(meta.redirect || path)
            if (path && path !== this.currentPath && !isDupRoute) {
                this.$router.push(path)
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.main-nav {
    z-index: 7;
    ::v-deep.v-navigation-drawer__border {
        background-color: $separator !important;
    }
    .v-list {
        padding: 0;
        background: transparent;
        .v-list-item {
            height: 52px;
            &:hover {
                background: #eefafd !important;
            }
            .nav-item {
                height: 40px;
                &__icon {
                    height: 100%;
                    margin: 0;
                    align-items: center;
                    justify-content: center;
                }
                &__label {
                    color: $navigation;
                    font-size: 1rem;
                }
                &--active {
                    background-color: $separator;
                    border-radius: 8px;
                    .nav-item__label {
                        color: $blue-azure;
                    }
                }
            }
        }
    }
}
</style>
