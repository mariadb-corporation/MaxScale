<template>
    <div class="fill-height">
        <v-card v-if="loading_db_tree" class="fill-height db-tb-list" :loading="loading_db_tree" />
        <v-fade-transition>
            <div
                v-if="!loading_db_tree"
                class="db-tb-list"
                :class="[is_sidebar_collapsed ? 'pa-1' : 'pa-3']"
            >
                <div class="visible-when-expand fill-height">
                    <div class="schema-list-tools">
                        <div class="d-flex align-center justify-end">
                            <span
                                v-if="!is_sidebar_collapsed"
                                class="color text-small-text db-tb-list__title d-inline-block text-truncate text-uppercase"
                            >
                                {{ $t('schemas') }}
                            </span>
                            <v-tooltip
                                v-if="!is_sidebar_collapsed"
                                top
                                transition="slide-y-transition"
                                content-class="shadow-drop color text-navigation py-1 px-4"
                            >
                                <template v-slot:activator="{ on }">
                                    <v-btn
                                        icon
                                        small
                                        :disabled="!active_conn_state"
                                        v-on="on"
                                        @click="reloadSchema"
                                    >
                                        <v-icon size="12" color="deep-ocean">
                                            $vuetify.icons.reload
                                        </v-icon>
                                    </v-btn>
                                </template>
                                <span>{{ $t('reload') }}</span>
                            </v-tooltip>
                            <v-tooltip
                                top
                                transition="slide-y-transition"
                                content-class="shadow-drop color text-navigation py-1 px-4"
                            >
                                <template v-slot:activator="{ on }">
                                    <v-btn
                                        icon
                                        small
                                        v-on="on"
                                        @click="SET_IS_SIDEBAR_COLLAPSED(!is_sidebar_collapsed)"
                                    >
                                        <v-icon
                                            size="16"
                                            color="deep-ocean"
                                            class="collapse-icon"
                                            :class="{
                                                'collapse-icon--active': is_sidebar_collapsed,
                                            }"
                                        >
                                            double_arrow
                                        </v-icon>
                                    </v-btn>
                                </template>
                                <span>{{
                                    is_sidebar_collapsed ? $t('expand') : $t('collapse')
                                }}</span>
                            </v-tooltip>
                        </div>
                        <v-text-field
                            v-if="!is_sidebar_collapsed"
                            v-model="searchSchema"
                            name="searchSchema"
                            required
                            dense
                            outlined
                            height="28"
                            class="std filter-objects"
                            :placeholder="$t('filterSchemaObjects')"
                            :disabled="!active_conn_state"
                        />
                    </div>
                    <db-list-tree
                        v-show="active_conn_state && !is_sidebar_collapsed"
                        class="schema-list-wrapper"
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
                        v-on="$listeners"
                    />
                </div>
            </div>
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
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import DbListTree from './DbListTree'
import { mapState, mapActions, mapMutations } from 'vuex'
export default {
    name: 'sidebar-container',
    components: {
        DbListTree,
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            loading_db_tree: state => state.query.loading_db_tree,
            active_conn_state: state => state.query.active_conn_state,
            is_sidebar_collapsed: state => state.query.is_sidebar_collapsed,
            search_schema: state => state.query.search_schema,
        }),
        searchSchema: {
            get() {
                return this.search_schema
            },
            set(value) {
                this.SET_SEARCH_SCHEMA(value)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
            SET_IS_SIDEBAR_COLLAPSED: 'query/SET_IS_SIDEBAR_COLLAPSED',
            SET_SEARCH_SCHEMA: 'query/SET_SEARCH_SCHEMA',
        }),
        ...mapActions({
            clearDataPreview: 'query/clearDataPreview',
            fetchPrvw: 'query/fetchPrvw',
            updateTreeNodes: 'query/updateTreeNodes',
            useDb: 'query/useDb',
            reloadTreeNodes: 'query/reloadTreeNodes',
        }),
        async reloadSchema() {
            await this.reloadTreeNodes()
        },
        async handleFetchPreview({ SQL_QUERY_MODE, schemaId }) {
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
        async handleLoadChildren(node) {
            await this.updateTreeNodes(node)
        },
    },
}
</script>

<style lang="scss" scoped>
.db-tb-list {
    border: 1px solid $table-border;
    border-top: none;
    width: 100%;
    height: 100%;
    .db-tb-list__title {
        font-size: 12px;
        margin-right: auto;
    }
    .collapse-icon {
        transform: rotate(-180deg);
        &--active {
            transform: rotate(0deg);
        }
    }
    ::v-deep .std.filter-objects {
        input {
            font-size: 12px;
        }
    }
    $tools-height: 60px;
    .schema-list-tools {
        height: $tools-height;
    }
    .schema-list-wrapper {
        font-size: 12px;
        max-height: calc(100% - #{$tools-height});
        overflow-y: auto;
        z-index: 1;
    }
}
</style>
