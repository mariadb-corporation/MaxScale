<template>
    <div class="fill-height">
        <v-card v-if="loading_db_tree" class="fill-height db-tb-list" :loading="loading_db_tree" />
        <v-fade-transition>
            <db-list
                v-if="!loading_db_tree"
                class="db-tb-list"
                :schemaList="db_tree"
                :disabled="!active_conn_state"
                v-on="$listeners"
                @reload-schema="loadSchema"
                @preview-data="
                    schemaId =>
                        handleFetchPreview({
                            SQL_QUERY_MODE: SQL_QUERY_MODES.PRVW_DATA,
                            schemaId,
                        })
                "
                @view-details="
                    schemaId =>
                        handleFetchPreview({
                            SQL_QUERY_MODE: SQL_QUERY_MODES.PRVW_DATA_DETAILS,
                            schemaId,
                        })
                "
                @load-children="handleLoadChildren"
                @use-db="useDb"
            />
        </v-fade-transition>
    </div>
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
import DbList from './DbList'
import { mapState, mapActions, mapMutations } from 'vuex'
export default {
    name: 'sidebar-container',
    components: {
        DbList,
    },
    computed: {
        ...mapState({
            checking_active_conn: state => state.query.checking_active_conn,
            loading_db_tree: state => state.query.loading_db_tree,
            db_tree: state => state.query.db_tree,
            active_conn_state: state => state.query.active_conn_state,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
        }),
    },
    watch: {
        checking_active_conn: async function(v) {
            // after finish checking active connection
            if (!v && this.active_conn_state)
                // auto load schema when there is active connection
                await this.loadSchema()
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchDbList: 'query/fetchDbList',
            clearDataPreview: 'query/clearDataPreview',
            fetchPrvw: 'query/fetchPrvw',
            fetchTables: 'query/fetchTables',
            fetchCols: 'query/fetchCols',
            useDb: 'query/useDb',
        }),
        async loadSchema() {
            await this.fetchDbList()
        },
        async handleFetchPreview({ SQL_QUERY_MODE, schemaId }) {
            this.$emit('get-curr-prvw-data-schemaId', schemaId)
            this.clearDataPreview()
            this.SET_CURR_QUERY_MODE(SQL_QUERY_MODE)
            switch (SQL_QUERY_MODE) {
                case this.SQL_QUERY_MODES.PRVW_DATA:
                case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    await this.fetchPrvw({
                        tblId: schemaId,
                        prvwMode: SQL_QUERY_MODE,
                    })
                    break
            }
        },
        async handleLoadChildren(item) {
            if (!item.id.includes('.')) await this.fetchTables(item)
            else await this.fetchCols(item)
        },
    },
}
</script>

<style lang="scss" scoped>
.db-tb-list {
    border: 1px solid $table-border;
    width: 100%;
    height: 100%;
}
</style>
