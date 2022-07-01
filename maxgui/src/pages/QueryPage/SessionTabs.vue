<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeSessionId"
            show-arrows
            hide-slider
            :height="30"
            class="tab-navigation--btn-style session-navigation flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${sessionToolbarWidth + 1}px)` }"
        >
            <v-tab
                v-for="session in getSessionsOfActiveWke"
                :key="`${session.id}`"
                :href="`#${session.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <div
                    style="min-width:160px"
                    class="fill-height d-flex align-center justify-space-between px-3"
                >
                    <div class="d-inline-flex align-center">
                        <truncate-string
                            :key="`${session.name}`"
                            :text="`${session.name}`"
                            :maxWidth="112"
                            :nudgeLeft="48"
                        />
                        <span
                            v-if="getIsFileUnsavedBySessionId(session.id)"
                            class="unsaved-changes-indicator"
                        />
                        <v-progress-circular
                            v-if="getLoadingQueryResultBySessionId(session.id)"
                            class="ml-2"
                            size="16"
                            width="2"
                            color="primary"
                            indeterminate
                        />
                    </div>
                    <v-btn
                        class="ml-1 del-session-btn"
                        icon
                        x-small
                        :disabled="$typy(is_conn_busy_map[session.id], 'value').safeBoolean"
                        @click="
                            getIsFileUnsaved ? openConfDlg(session) : handleDeleteSessTab(session)
                        "
                    >
                        <v-icon
                            size="8"
                            :color="
                                $typy(is_conn_busy_map[session.id], 'value').safeBoolean
                                    ? ''
                                    : 'error'
                            "
                        >
                            $vuetify.icons.close
                        </v-icon>
                    </v-btn>
                </div>
            </v-tab>
        </v-tabs>
        <session-tabs-toolbar @get-total-btn-width="sessionToolbarWidth = $event" />
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
import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
import SessionTabsToolbar from './SessionTabsToolbar.vue'
export default {
    name: 'session-tabs',
    components: { 'session-tabs-toolbar': SessionTabsToolbar },
    props: {
        sessionToolbarRef: { type: Object, required: true },
    },
    data() {
        return {
            sessionToolbarWidth: 0,
        }
    },
    computed: {
        ...mapState({
            is_conn_busy_map: state => state.queryConn.is_conn_busy_map,
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getActiveSession: 'querySession/getActiveSession',
            getSessionsOfActiveWke: 'querySession/getSessionsOfActiveWke',
            getLoadingQueryResultBySessionId: 'queryResult/getLoadingQueryResultBySessionId',
            getIsFileUnsavedBySessionId: 'editor/getIsFileUnsavedBySessionId',
            getIsFileUnsaved: 'editor/getIsFileUnsaved',
        }),
        activeSessionId: {
            get() {
                return this.getActiveSessionId
            },
            set(v) {
                this.SET_ACTIVE_SESSION_BY_WKE_ID_MAP({
                    payload: v,
                    id: this.active_wke_id,
                })
            },
        },
    },
    activated() {
        this.addGetActiveSessionIdWatcher()
    },
    deactivated() {
        this.rmGetActiveSessionIdWatcher()
    },
    methods: {
        ...mapMutations({
            SET_ACTIVE_SESSION_BY_WKE_ID_MAP: 'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
        }),
        ...mapActions({
            handleDeleteSession: 'querySession/handleDeleteSession',
            handleSyncSession: 'querySession/handleSyncSession',
            handleClearTheLastSession: 'querySession/handleClearTheLastSession',
        }),
        addGetActiveSessionIdWatcher() {
            this.rmGetActiveSessionIdWatcher = this.$watch(
                'getActiveSessionId',
                v => {
                    if (v) this.handleSyncSession(this.getActiveSession)
                },
                { immediate: true }
            )
        },
        /**
         * @param {Object} session - session object
         */
        openConfDlg(session) {
            const loadSql = this.$typy(this.sessionToolbarRef, '$refs.loadSql').safeObject
            loadSql.confDlg = {
                ...loadSql.confDlg,
                isOpened: true,
                title: this.$t('deleteSession'),
                type: 'deleteSession',
                confirmMsg: this.$t('confirmations.deleteSession', { targetId: session.name }),
                onSave: async () => {
                    await loadSql.handleSaveScript()
                    await this.handleDeleteSessTab(session)
                },
                dontSave: async () => await this.handleDeleteSessTab(session),
            }
        },
        async handleDeleteSessTab(session) {
            if (this.getSessionsOfActiveWke.length === 1) this.handleClearTheLastSession(session)
            else await this.handleDeleteSession(session)
        },
    },
}
</script>
<style lang="scss" scoped>
.unsaved-changes-indicator::after {
    content: ' *';
    color: $primary;
    padding-left: 4px;
    font-size: 0.875rem;
    position: relative;
    font-weight: 500;
    font-family: $heading-font-family;
}
</style>
