<template>
    <v-toolbar
        outlined
        elevation="0"
        height="45"
        class="wke-toolbar"
        :class="{ 'ml-0': is_fullscreen }"
    >
        <connection-manager :disabled="getIsQuerying" />
        <!-- Use database section-->

        <v-menu
            transition="slide-y-transition"
            offset-y
            content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
            :max-width="200"
        >
            <template v-slot:activator="{ on: menu, attrs }">
                <v-tooltip
                    :disabled="!active_db"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on: tooltip }">
                        <v-btn
                            outlined
                            max-width="160"
                            class="ml-2 text-none px-2 font-weight-regular use-db-btn"
                            depressed
                            small
                            color="accent-dark"
                            :disabled="!hasActiveConn || getIsQuerying"
                            v-bind="attrs"
                            v-on="{ ...tooltip, ...menu }"
                        >
                            <v-icon class="mr-1" size="16">
                                $vuetify.icons.database
                            </v-icon>
                            <div
                                class="d-inline-block text-truncate"
                                :style="{ maxWidth: `122px` }"
                            >
                                {{ active_db ? active_db : $t('useDb') }}
                            </div>
                        </v-btn>
                    </template>
                    <span>{{ $t('useDb') }}: {{ active_db }} </span>
                </v-tooltip>
            </template>
            <v-list>
                <v-list-item
                    v-for="db in getDbNodes"
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

        <v-spacer></v-spacer>
        <portal-target name="wke-toolbar-right">
            <!-- Run section-->
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
                        :loading="getLoadingQueryResult"
                        :disabled="
                            !query_txt ||
                                !hasActiveConn ||
                                (getIsQuerying && !getLoadingQueryResult)
                        "
                        v-on="on"
                        @click="() => handleRun(selected_query_txt ? 'selected' : 'all')"
                    >
                        <v-icon size="16" class="mr-2">
                            $vuetify.icons.running
                        </v-icon>
                        {{ $t('run') }}
                    </v-btn>
                </template>
                <span style="white-space: pre;" class="d-inline-block text-center">
                    {{
                        selected_query_txt
                            ? `${$t('runStatements', {
                                  quantity: $t('selected'),
                              })}\nCmd/Ctrl + Enter`
                            : `${$t('runStatements', {
                                  quantity: $t('all'),
                              })}\nCmd/Ctrl + Shift + Enter`
                    }}
                </span>
            </v-tooltip>
            <!-- Visualize section-->
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        class="ml-2 visualize-btn"
                        :outlined="!show_vis_sidebar"
                        depressed
                        small
                        :color="show_vis_sidebar ? 'primary' : 'accent-dark'"
                        :disabled="!hasActiveConn || getIsQuerying"
                        v-on="on"
                        @click="SET_SHOW_VIS_SIDEBAR(!show_vis_sidebar)"
                    >
                        <v-icon
                            size="16"
                            :color="
                                hasActiveConn
                                    ? show_vis_sidebar
                                        ? 'background'
                                        : 'accent-dark'
                                    : ''
                            "
                        >
                            $vuetify.icons.reports
                        </v-icon>
                    </v-btn>
                </template>
                <span class="text-capitalize">
                    {{
                        $t('visualizedConfig', {
                            action: show_vis_sidebar ? $t('hide') : $t('show'),
                        })
                    }}
                </span>
            </v-tooltip>
            <confirm-dialog
                v-if="query_confirm_flag"
                v-model="isConfDlgOpened"
                :title="$t('confirmations.runQuery')"
                type="run"
                minBodyWidth="768px"
                closeImmediate
                :onSave="confirmRunning"
            >
                <template v-slot:body-prepend>
                    <div class="mb-4 readonly-sql-code-wrapper pa-2">
                        <readonly-query-editor
                            :value="activeRunMode === 'selected' ? selected_query_txt : query_txt"
                            class="readonly-editor fill-height"
                            readOnly
                            :options="{
                                fontSize: 10,
                                contextmenu: false,
                            }"
                        />
                    </div>
                </template>
                <template v-slot:action-prepend>
                    <v-checkbox
                        v-model="dontShowConfirm"
                        class="pa-0 ma-0"
                        :label="$t('dontAskMeAgain')"
                        color="primary"
                        hide-details
                    />
                    <v-spacer />
                </template>
            </confirm-dialog>
        </portal-target>
    </v-toolbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
import ConnectionManager from './ConnectionManager'
import QueryEditor from '@/components/QueryEditor'

export default {
    name: 'worksheet-toolbar',
    components: {
        ConnectionManager,
        'readonly-query-editor': QueryEditor,
    },
    data() {
        return {
            dontShowConfirm: false,
            activeRunMode: 'all',
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            is_fullscreen: state => state.query.is_fullscreen,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_db: state => state.query.active_db,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            show_vis_sidebar: state => state.query.show_vis_sidebar,
            query_txt: state => state.query.query_txt,
            selected_query_txt: state => state.query.selected_query_txt,
        }),
        ...mapGetters({
            getIsQuerying: 'query/getIsQuerying',
            getLoadingQueryResult: 'query/getLoadingQueryResult',
            getDbNodes: 'query/getDbNodes',
        }),
        //Prevent parallel querying
        shouldDisableExecute() {
            return (
                !this.query_txt ||
                !this.hasActiveConn ||
                (this.getIsQuerying && this.getLoadingQueryResult)
            )
        },
        hasActiveConn() {
            return this.$typy(this.curr_cnct_resource, 'id').isDefined
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_SHOW_VIS_SIDEBAR: 'query/SET_SHOW_VIS_SIDEBAR',
        }),
        ...mapActions({
            fetchQueryResult: 'query/fetchQueryResult',
            useDb: 'query/useDb',
        }),
        async handleSelectDb(db) {
            await this.useDb(db)
        },
        /**
         * Only open dialog when its corresponding query text exists
         */
        shouldOpenDialog(mode) {
            return (
                (mode === 'selected' && this.selected_query_txt) ||
                (mode === 'all' && this.query_txt)
            )
        },
        async handleRun(mode) {
            if (!this.shouldDisableExecute)
                if (!this.query_confirm_flag) await this.onRun(mode)
                else if (this.shouldOpenDialog(mode)) {
                    this.activeRunMode = mode
                    this.dontShowConfirm = false // clear checkbox state
                    this.isConfDlgOpened = true
                }
        },
        async confirmRunning() {
            if (this.dontShowConfirm) this.SET_QUERY_CONFIRM_FLAG(0)
            await this.onRun(this.activeRunMode)
        },
        /**
         * @param {String} mode Mode to execute query: All or selected
         */
        async onRun(mode) {
            this.SET_CURR_QUERY_MODE(this.SQL_QUERY_MODES.QUERY_VIEW)
            switch (mode) {
                case 'all':
                    if (this.query_txt) await this.fetchQueryResult(this.query_txt)
                    break
                case 'selected':
                    if (this.selected_query_txt)
                        await this.fetchQueryResult(this.selected_query_txt)
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

::v-deep.wke-toolbar {
    .v-toolbar__content {
        padding: 4px 12px;
    }
}
</style>
