<template>
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
                    :class="[getLoadingQueryResultBySessionId(session.id) ? 'stop-btn' : 'run-btn']"
                    text
                    color="accent-dark"
                    :disabled="
                        getLoadingQueryResultBySessionId(session.id) ? false : shouldDisableExecute
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
                                getLoadingQueryResultBySessionId(session.id) ? 'stopped' : 'running'
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
                            hasActiveConn ? (show_vis_sidebar ? 'background' : 'accent-dark') : ''
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
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="save-to-fav-btn"
                    icon
                    small
                    color="accent-dark"
                    :disabled="!query_txt"
                    v-on="on"
                    @click="openFavoriteDialog"
                >
                    <v-icon size="20">
                        mdi-bookmark
                    </v-icon>
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{
                    selected_query_txt
                        ? `${$t('saveStatementsToFavorite', {
                              quantity: $t('selected'),
                          })}\nCmd/Ctrl + S`
                        : `${$t('saveStatementsToFavorite', {
                              quantity: $t('all'),
                          })}\nCmd/Ctrl + S`
                }}
            </span>
        </v-tooltip>
        <!-- TODO: Move it to session-toolbar -->
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            minBodyWidth="768px"
            :closeImmediate="true"
            :onSave="confDlg.onSave"
        >
            <template v-slot:body-prepend>
                <div class="mb-4 readonly-sql-code-wrapper pa-2">
                    <readonly-query-editor
                        :value="confDlg.sqlTxt"
                        class="readonly-editor fill-height"
                        readOnly
                        :options="{
                            fontSize: 10,
                            contextmenu: false,
                        }"
                    />
                </div>
                <template v-if="confDlg.isSavingFavoriteQuery">
                    <label class="field__label color text-small-text label-required">
                        {{ $t('name') }}
                    </label>
                    <v-text-field
                        v-model="favorite.name"
                        type="text"
                        :rules="[
                            val => !!val || $t('errors.requiredInput', { inputName: $t('name') }),
                        ]"
                        class="std error--text__bottom mb-2"
                        dense
                        :height="36"
                        hide-details="auto"
                        outlined
                        required
                    />
                </template>
            </template>
            <template v-if="!confDlg.isSavingFavoriteQuery" v-slot:action-prepend>
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

/**
 * These session buttons in this component are associated with a session. In other words,
 * if there are n session objects in query_sessions, this component
 * will be rendered n times because parallel queries between session tabs are allowed, so
 * the states of these buttons depended upon the session data.
 */
import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'
import QueryEditor from '@/components/QueryEditor'
export default {
    name: 'session-btns',
    components: {
        'readonly-query-editor': QueryEditor,
    },
    props: {
        session: { type: Object, required: true },
        isMaxRowsValid: { type: Boolean, required: true },
    },
    data() {
        return {
            dontShowConfirm: false,
            activeRunMode: 'all',
            confDlg: {
                isOpened: false,
                title: this.$t('confirmations.runQuery'),
                type: 'run',
                sqlTxt: '',
                isSavingFavoriteQuery: false,
                onSave: () => null,
            },
            favorite: { date: '', name: '' },
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
    deactivated() {
        if (this.rmIsQueryKilledWatcher) this.rmIsQueryKilledWatcher()
    },
    activated() {
        this.addIsQueryKilledWatcher()
    },
    methods: {
        ...mapActions({
            fetchQueryResult: 'queryResult/fetchQueryResult',
            stopQuery: 'queryResult/stopQuery',
            disconnectClone: 'queryConn/disconnectClone',
            pushQueryFavorite: 'persisted/pushQueryFavorite',
        }),
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_SHOW_VIS_SIDEBAR: 'queryResult/SET_SHOW_VIS_SIDEBAR',
        }),
        addIsQueryKilledWatcher() {
            this.rmIsQueryKilledWatcher = this.$watch('isQueryKilled', async (v, oV) => {
                if (v !== oV && v) {
                    const bgConn = this.getCloneConn({
                        clone_of_conn_id: this.session.active_sql_conn.id,
                        binding_type: this.QUERY_CONN_BINDING_TYPES.BACKGROUND,
                    })
                    if (bgConn.id) await this.disconnectClone({ id: bgConn.id })
                }
            })
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
                    this.confDlg = {
                        ...this.confDlg,
                        isOpened: true,
                        title: this.$t('confirmations.runQuery'),
                        type: 'run',
                        isSavingFavoriteQuery: false,
                        sqlTxt:
                            this.activeRunMode === 'selected'
                                ? this.selected_query_txt
                                : this.query_txt,
                        onSave: this.confirmRunning,
                    }
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
        openFavoriteDialog() {
            if (this.query_txt) {
                this.favorite.date = new Date().valueOf()
                this.favorite.name = `Favorite statements - ${this.$help.dateFormat({
                    value: this.favorite.date,
                    formatType: 'DATE_RFC2822',
                })}`
                this.confDlg = {
                    ...this.confDlg,
                    isOpened: true,
                    title: this.$t('confirmations.addToFavorite'),
                    type: 'add',
                    isSavingFavoriteQuery: true,
                    sqlTxt: this.selected_query_txt ? this.selected_query_txt : this.query_txt,
                    onSave: this.addToFavorite,
                }
            }
        },
        addToFavorite() {
            let payload = {
                sql: this.query_txt,
                ...this.favorite,
            }
            if (this.selected_query_txt) payload.sql = this.selected_query_txt
            this.pushQueryFavorite(payload)
        },
    },
}
</script>
<style lang="scss" scoped>
.icon-group {
    height: 28px;
    ::v-deep .v-btn {
        min-width: unset !important;
        padding: 0px !important;
        width: 28px;
        height: 28px;
        border-radius: 0px !important;
        &:hover {
            border-radius: 0px !important;
        }
    }
}
</style>
