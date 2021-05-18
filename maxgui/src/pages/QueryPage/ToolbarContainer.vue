<template>
    <v-toolbar
        outlined
        elevation="0"
        height="50"
        class="query-toolbar border-bottom-none"
        :class="{ 'ml-0': isFullScreen }"
    >
        <v-toolbar-title class="color text-navigation text-capitalize">
            {{ $route.name }}
        </v-toolbar-title>

        <v-spacer></v-spacer>
        <connection-manager />
        <v-btn
            width="80"
            outlined
            height="36"
            rounded
            class="ml-4 text-capitalize px-8 font-weight-medium"
            depressed
            small
            color="accent-dark"
            :disabled="!queryTxt || !active_conn_state"
            @click="onRun"
        >
            {{ $t('run') }}
        </v-btn>
    </v-toolbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState, mapMutations } from 'vuex'
import ConnectionManager from './ConnectionManager'
export default {
    name: 'toolbar-container',
    components: {
        ConnectionManager,
    },
    props: {
        isFullScreen: { type: Boolean, required: true },
        queryTxt: { type: String, required: true },
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            active_conn_state: state => state.query.active_conn_state,
        }),
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchQueryResult: 'query/fetchQueryResult',
        }),
        async onRun() {
            this.SET_CURR_QUERY_MODE(this.SQL_QUERY_MODES.QUERY_VIEW)
            await this.fetchQueryResult(this.queryTxt)
        },
    },
}
</script>
