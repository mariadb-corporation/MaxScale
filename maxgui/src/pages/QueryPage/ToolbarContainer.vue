<template>
    <v-toolbar
        outlined
        elevation="0"
        height="45"
        class="query-toolbar"
        :class="{ 'ml-0': is_fullscreen }"
    >
        <connection-manager :disabled="getLoadingQueryResult" />
        <!-- Use database section-->
        <v-btn
            id="active-db"
            outlined
            max-width="160"
            class="ml-2 text-none px-2 font-weight-regular"
            depressed
            small
            color="accent-dark"
            :disabled="!curr_cnct_resource.id || getLoadingQueryResult"
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
        <v-tooltip
            v-if="active_db"
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
            activator="#active-db"
        >
            <span>{{ $t('useDb') }}: {{ active_db }} </span>
        </v-tooltip>

        <v-spacer></v-spacer>
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
                    :disabled="!query_txt.all || !curr_cnct_resource.id"
                    v-on="on"
                    @click="() => handleRun(query_txt.selected ? 'selected' : 'all')"
                >
                    <v-icon size="16" class="mr-2">
                        $vuetify.icons.running
                    </v-icon>
                    {{ $t('run') }}
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ query_txt.selected ? $t('runSelectedStatements') : $t('runAllStatements') }}
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
                    class="ml-2 visualize"
                    :outlined="!show_vis_sidebar"
                    depressed
                    small
                    :color="show_vis_sidebar ? 'primary' : 'accent-dark'"
                    :disabled="!curr_cnct_resource.id || getLoadingQueryResult"
                    v-on="on"
                    @click="SET_SHOW_VIS_SIDEBAR(!show_vis_sidebar)"
                >
                    <v-icon
                        size="16"
                        :color="
                            curr_cnct_resource.id
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
                {{ $t('visualizedConfig', { action: show_vis_sidebar ? $t('hide') : $t('show') }) }}
            </span>
        </v-tooltip>
        <!-- Settings section-->
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="ml-2"
                    icon
                    small
                    v-on="on"
                    @click="queryConfigDialog = !queryConfigDialog"
                >
                    <v-icon size="16" color="accent-dark">
                        $vuetify.icons.settings
                    </v-icon>
                </v-btn>
            </template>
            <span class="text-capitalize"> {{ $tc('settings', 2) }}</span>
        </v-tooltip>

        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn icon small v-on="on" @click="SET_FULLSCREEN(!is_fullscreen)">
                    <v-icon size="20" color="accent-dark">
                        fullscreen{{ is_fullscreen ? '_exit' : '' }}
                    </v-icon>
                </v-btn>
            </template>
            <span>{{ is_fullscreen ? $t('minimize') : $t('maximize') }}</span>
        </v-tooltip>

        <query-config-dialog v-model="queryConfigDialog" />

        <confirm-dialog
            v-if="query_confirm_flag"
            ref="runConfirmDialog"
            :title="$t('confirmations.runQuery')"
            type="run"
            :onSave="confirmRunning"
            minBodyWidth="768px"
            closeImmediate
        >
            <template v-slot:body-prepend>
                <div class="mb-4 sql-code-wrapper pa-2">
                    <readonly-query-editor
                        :value="query_txt.selected ? query_txt.selected : query_txt.all"
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
    </v-toolbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
import ConnectionManager from './ConnectionManager'
import QueryEditor from '@/components/QueryEditor'
import QueryConfigDialog from './QueryConfigDialog'
export default {
    name: 'toolbar-container',
    components: {
        ConnectionManager,
        'readonly-query-editor': QueryEditor,
        'query-config-dialog': QueryConfigDialog,
    },
    data() {
        return {
            dontShowConfirm: false,
            queryConfigDialog: false,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.query.is_fullscreen,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_db: state => state.query.active_db,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            show_vis_sidebar: state => state.query.show_vis_sidebar,
            query_txt: state => state.query.query_txt,
        }),
        ...mapGetters({
            getLoadingQueryResult: 'query/getLoadingQueryResult',
            getDbNodes: 'query/getDbNodes',
        }),
    },
    methods: {
        ...mapMutations({
            SET_FULLSCREEN: 'query/SET_FULLSCREEN',
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

        async handleRun(mode) {
            if (!this.query_confirm_flag) await this.onRun(mode)
            else {
                this.dontShowConfirm = false // clear checkbox state
                this.$refs.runConfirmDialog.open()
            }
        },
        async confirmRunning() {
            await this.onRun(this.query_txt.selected ? 'selected' : 'all')
            if (this.dontShowConfirm) this.SET_QUERY_CONFIRM_FLAG(0)
        },
        /**
         * @param {String} mode Mode to execute query: All or selected
         */
        async onRun(mode) {
            if (this.getLoadingQueryResult) return null
            this.SET_CURR_QUERY_MODE(this.SQL_QUERY_MODES.QUERY_VIEW)
            switch (mode) {
                case 'all':
                    if (this.query_txt.all) await this.fetchQueryResult(this.query_txt.all)
                    break
                case 'selected':
                    if (this.query_txt.selected)
                        await this.fetchQueryResult(this.query_txt.selected)
                    break
            }
        },
        onSaveConfig() {},
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
.sql-code-wrapper {
    background-color: $reflection;
    height: 300px;
}
::v-deep .readonly-editor {
    .overflow-guard {
        .monaco-editor,
        .monaco-editor-background,
        .monaco-editor .inputarea.ime-input {
            background-color: #e8eef1;
        }
        .margin {
            background-color: #e8eef1;
        }
        .view-line {
            background-color: $reflection;
        }
    }
}
::v-deep.query-toolbar {
    .v-toolbar__content {
        padding: 4px 12px;
    }
}
</style>
