<template>
    <v-app-bar height="64px" class="pl-8" fixed clipped-left app flat color="blue-azure">
        <v-toolbar-title class="app-headline text-h5">
            <router-link to="/dashboard/servers">
                <img src="@share/assets/logo.svg" alt="MariaDB Logo" />
                <span class="product-name tk-azo-sans-web font-weight-medium  white--text">
                    MaxScale
                </span>
            </router-link>
        </v-toolbar-title>

        <v-spacer></v-spacer>

        <v-menu
            v-model="isProfileOpened"
            allow-overflow
            transition="slide-y-transition"
            offset-y
            content-class="v-menu--mariadb "
        >
            <template v-slot:activator="{ on }">
                <v-btn dark class="arrow-toggle" text tile v-on="on">
                    <v-icon class="mr-1 " size="30">
                        $vuetify.icons.mxs_user
                    </v-icon>
                    <span class="user-name tk-adrianna text-capitalize font-weight-regular">
                        {{ logged_in_user ? logged_in_user.name : '' }}
                    </span>
                    <v-icon
                        :class="[isProfileOpened ? 'rotate-up' : 'rotate-down']"
                        size="14"
                        class="mr-0 ml-1"
                        left
                    >
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                </v-btn>
            </template>

            <v-list>
                <v-list-item @click="handleLogout">
                    <v-list-item-title>{{ $mxs_t('logout') }}</v-list-item-title>
                </v-list-item>
            </v-list>
        </v-menu>
    </v-app-bar>
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
import { mapActions, mapState } from 'vuex'
import QueryConn from '@wsModels/QueryConn'
import { abortRequests } from '@src/utils/axios'

export default {
    name: 'app-header',
    data() {
        return {
            items: [],
            isProfileOpened: false,
        }
    },
    computed: {
        ...mapState('user', {
            logged_in_user: state => state.logged_in_user,
        }),
    },
    methods: {
        ...mapActions('user', ['logout']),
        async handleLogout() {
            abortRequests() // abort all previous pending requests before logging out
            // Disconnect all workspace connections
            await QueryConn.dispatch('disconnectAll')
            await this.logout()
        },
    },
}
</script>
<style lang="scss" scoped>
.app-headline {
    a {
        text-decoration: none;
    }

    img {
        vertical-align: middle;
        width: 155px;
        height: 38px;
    }

    .product-name {
        position: relative;
        vertical-align: middle;
        font-size: 1.125rem;
    }
}
.user-name {
    font-size: 1rem;
}
.v-btn {
    letter-spacing: normal;
}
</style>
