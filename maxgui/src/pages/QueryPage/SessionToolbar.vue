<template>
    <div class="session-toolbar d-flex align-center">
        <div class="d-inline-flex justify-center align-center icon-group">
            <!-- Run/Stop buttons-->
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
                :disabled="getLoadingQueryResultBySessionId(session.id)"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        :class="[
                            getLoadingQueryResultBySessionId(session.id) ? 'stop-btn' : 'run-btn',
                        ]"
                        text
                        color="accent-dark"
                        :disabled="
                            getLoadingQueryResultBySessionId(session.id)
                                ? false
                                : shouldDisableExecute
                        "
                        v-on="on"
                        @click="
                            () =>
                                getLoadingQueryResultBySessionId(session.id)
                                    ? stopQuery()
                                    : handleRun(selected_query_txt ? 'selected' : 'all')
                        "
                    >
                        <v-icon size="16">
                            {{
                                `$vuetify.icons.${
                                    getLoadingQueryResultBySessionId(session.id)
                                        ? 'stopped'
                                        : 'running'
                                }`
                            }}
                        </v-icon>
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
                        class="visualize-btn"
                        :depressed="show_vis_sidebar"
                        :text="!show_vis_sidebar"
                        :color="show_vis_sidebar ? 'primary' : 'accent-dark'"
                        :disabled="!hasActiveConn || getIsConnBusyBySessionId(session.id)"
                        v-on="on"
                        @click="
                            SET_SHOW_VIS_SIDEBAR({
                                payload: !show_vis_sidebar,
                                id: getActiveSessionId,
                            })
                        "
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
        </div>
        <v-spacer />
        <v-form v-model="isMaxRowsValid">
            <max-rows-input
                :style="{ maxWidth: '180px' }"
                :height="28"
                hide-details="auto"
                :hasFieldsetBorder="false"
                @change="SET_QUERY_MAX_ROW($event)"
            >
                <template v-slot:prepend-inner>
                    <label class="field__label color text-small-text">
                        {{ $t('maxRows') }}
                    </label>
                </template>
            </max-rows-input>
        </v-form>
        <confirm-dialog
            v-if="query_confirm_flag"
            v-model="isConfDlgOpened"
            :title="$t('confirmations.runQuery')"
            type="run"
            minBodyWidth="768px"
            :closeImmediate="true"
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
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'
import QueryEditor from '@/components/QueryEditor'
import MaxRowsInput from './MaxRowsInput.vue'
export default {
    name: 'session-toolbar',
    components: {
        'readonly-query-editor': QueryEditor,
        'max-rows-input': MaxRowsInput,
    },
    props: { session: { type: Object, required: true } },
    data() {
        return {
            dontShowConfirm: false,
            activeRunMode: 'all',
            isConfDlgOpened: false,
            isMaxRowsValid: true,
        }
    },
    computed: {
        ...mapState({
            active_sql_conn: state => state.queryConn.active_sql_conn,
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            query_txt: state => state.editor.query_txt,
            selected_query_txt: state => state.editor.selected_query_txt,
            QUERY_CONN_BINDING_TYPES: state => state.app_config.QUERY_CONN_BINDING_TYPES,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
        }),
        ...mapGetters({
            getIsConnBusyBySessionId: 'queryConn/getIsConnBusyBySessionId',
            getLoadingQueryResultBySessionId: 'queryResult/getLoadingQueryResultBySessionId',
            getIsStoppingQueryBySessionId: 'queryResult/getIsStoppingQueryBySessionId',
            getCloneConn: 'queryConn/getCloneConn',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        //Prevent parallel querying
        shouldDisableExecute() {
            return (
                !this.query_txt ||
                !this.hasActiveConn ||
                (this.getIsConnBusyBySessionId(this.session.id) &&
                    this.getLoadingQueryResultBySessionId(this.session.id)) ||
                !this.isMaxRowsValid
            )
        },
        hasActiveConn() {
            return this.$typy(this.active_sql_conn, 'id').isDefined
        },
        isQueryKilled() {
            return (
                !this.getLoadingQueryResultBySessionId(this.session.id) &&
                !this.getIsStoppingQueryBySessionId(this.session.id)
            )
        },
    },
    watch: {
        async isQueryKilled(v) {
            if (v) {
                const bgConn = this.getCloneConn({
                    clone_of_conn_id: this.session.active_sql_conn.id,
                    binding_type: this.QUERY_CONN_BINDING_TYPES.BACKGROUND,
                })
                await this.disconnectClone({ id: bgConn.id })
            }
        },
    },
    methods: {
        ...mapActions({
            fetchQueryResult: 'queryResult/fetchQueryResult',
            stopQuery: 'queryResult/stopQuery',
            disconnectClone: 'queryConn/disconnectClone',
        }),
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_SHOW_VIS_SIDEBAR: 'queryResult/SET_SHOW_VIS_SIDEBAR',
            SET_QUERY_MAX_ROW: 'persisted/SET_QUERY_MAX_ROW',
        }),
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
            this.SET_CURR_QUERY_MODE({
                payload: this.SQL_QUERY_MODES.QUERY_VIEW,
                id: this.getActiveSessionId,
            })
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
.session-toolbar {
    border-bottom: 1px solid $table-border;
    border-right: 1px solid $table-border;
    width: 100%;
    .icon-group {
        height: 30px;
        ::v-deep .v-btn {
            min-width: unset !important;
            padding: 0px !important;
            width: 30px;
            height: 30px;
            border-radius: 0px !important;
            &:hover {
                border-radius: 0px !important;
            }
        }
    }
}
</style>
