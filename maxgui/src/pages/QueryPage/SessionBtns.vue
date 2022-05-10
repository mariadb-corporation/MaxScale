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
                <!-- disable button Prevent parallel querying of the same connection -->
                <v-btn
                    :class="[getLoadingQueryResultBySessionId(session.id) ? 'stop-btn' : 'run-btn']"
                    text
                    color="accent-dark"
                    :disabled="
                        getLoadingQueryResultBySessionId(session.id)
                            ? false
                            : getShouldDisableExecuteMap[session.id]
                    "
                    v-on="on"
                    @click="
                        () =>
                            getLoadingQueryResultBySessionId(session.id)
                                ? $emit('on-stop-query')
                                : $emit('on-run', selected_query_txt ? 'selected' : 'all')
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
                    :disabled="getShouldDisableExecuteMap[session.id]"
                    v-on="on"
                    @click="$emit('on-visualize')"
                >
                    <v-icon size="16"> $vuetify.icons.reports </v-icon>
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
 *
 * Emits:
 * - $emit('on-stop-query')
 * - $emit('on-run', enum: 'selected' ||  'all')
 * - $emit('on-visualize')
 */
import { mapActions, mapState, mapGetters } from 'vuex'

export default {
    name: 'session-btns',
    props: {
        session: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            active_sql_conn: state => state.queryConn.active_sql_conn,
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            selected_query_txt: state => state.editor.selected_query_txt,
            QUERY_CONN_BINDING_TYPES: state => state.app_config.QUERY_CONN_BINDING_TYPES,
        }),
        ...mapGetters({
            getLoadingQueryResultBySessionId: 'queryResult/getLoadingQueryResultBySessionId',
            getIsStoppingQueryBySessionId: 'queryResult/getIsStoppingQueryBySessionId',
            getCloneConn: 'queryConn/getCloneConn',
            getShouldDisableExecuteMap: 'queryResult/getShouldDisableExecuteMap',
        }),
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
        ...mapActions({ disconnectClone: 'queryConn/disconnectClone' }),
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
