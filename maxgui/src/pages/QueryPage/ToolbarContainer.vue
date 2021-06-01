<template>
    <v-toolbar
        outlined
        elevation="0"
        height="45"
        class="query-toolbar"
        :class="{ 'ml-0': isFullScreen }"
    >
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    outlined
                    class="run-btn text-capitalize px-2 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
                    :loading="loading_query_result"
                    :disabled="!queryTxt || !active_conn_state"
                    v-on="on"
                    @click="() => onRun(selectedQueryTxt ? 'selected' : 'all')"
                >
                    <v-icon size="16" class="mr-2">
                        $vuetify.icons.running
                    </v-icon>
                    {{ $t('run') }}
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ selectedQueryTxt ? $t('runSelectedStatements') : $t('runAllStatements') }}
            </span>
        </v-tooltip>
        <v-spacer></v-spacer>
        <connection-manager />
        <v-btn
            id="active-db"
            outlined
            max-width="160"
            class="ml-2 text-none px-2 font-weight-regular"
            depressed
            small
            color="accent-dark"
            :disabled="!active_conn_state"
        >
            <v-icon class="mr-1" size="16">
                $vuetify.icons.database
            </v-icon>
            <div class="d-inline-block text-truncate" :style="{ maxWidth: `122px` }">
                {{ active_db ? active_db : $t('useDb') }}
            </div>
        </v-btn>
        <v-menu
            transition="slide-y-transition"
            offset-y
            content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
            activator="#active-db"
            :max-width="200"
            :menuMaxWidth="400"
        >
            <v-list>
                <v-list-item
                    v-for="db in db_tree"
                    :key="db.id"
                    dense
                    link
                    @click="() => handleSelectDb(db.id)"
                >
                    <v-list-item-title class="color text-text">
                        <truncate-string :text="db.name" :maxWidth="166" :nudgeLeft="16" />
                    </v-list-item-title>
                </v-list-item>
            </v-list>
        </v-menu>
        <v-tooltip
            v-if="active_db"
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
            activator="#active-db"
        >
            <span>{{ $t('useDb') }}: {{ active_db }} </span>
        </v-tooltip>
        <!-- TODO: ADD gear icon for query settings.
             e.g. confirmation before sending query, configure query max rows
        -->
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
        selectedQueryTxt: { type: String, required: true },
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            active_conn_state: state => state.query.active_conn_state,
            active_db: state => state.query.active_db,
            db_tree: state => state.query.db_tree,
            loading_query_result: state => state.query.loading_query_result,
        }),
    },

    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchQueryResult: 'query/fetchQueryResult',
            useDb: 'query/useDb',
        }),
        async handleSelectDb(db) {
            await this.useDb(db)
        },
        /**
         * @param {String} mode Mode to execute query: All or selected
         */
        async onRun(mode) {
            this.SET_CURR_QUERY_MODE(this.SQL_QUERY_MODES.QUERY_VIEW)
            switch (mode) {
                case 'all':
                    if (this.queryTxt) await this.fetchQueryResult(this.queryTxt)
                    break
                case 'selected':
                    if (this.selectedQueryTxt) await this.fetchQueryResult(this.selectedQueryTxt)
                    break
            }
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.run-btn {
    .v-progress-circular {
        height: 16px !important;
        width: 16px !important;
    }
}
</style>
