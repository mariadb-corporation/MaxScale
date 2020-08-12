<template>
    <collapse
        :toggleOnClick="() => (showSessions = !showSessions)"
        :isContentVisible="showSessions"
        :title="`${$tc('currentSessions', 2)}`"
        :titleInfo="sessionsTableRow.length"
    >
        <template v-slot:content>
            <data-table
                tableClass="data-table-full--max-width-columns"
                :search="search_keyword"
                :headers="sessionsTableHeader"
                :data="sessionsTableRow"
                :sortDesc="false"
                :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
                :loading="loading"
            >
                <template v-slot:user="{ data: { item: { user } } }">
                    <router-link :key="user" :to="`/users/${user}`" class="no-underline">
                        <span> {{ user }} </span>
                    </router-link>
                </template>
                <template v-slot:connected="{ data: { item: { connected } } }">
                    <span> {{ $help.formatValue(connected) }} </span>
                </template>
            </data-table>
        </template>
    </collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'

export default {
    name: 'session-table',
    props: {
        loading: { type: Boolean, required: true },
    },
    data() {
        return {
            // sessions
            showSessions: true,
            sessionsTableHeader: [
                { text: 'ID', value: 'id' },
                { text: 'Client', value: 'user' },
                { text: 'Connected', value: 'connected' },
                { text: 'IDLE (s)', value: 'idle' },
            ],
            loop: true,
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            current_server: state => state.server.current_server,
            all_sessions: state => state.session.all_sessions,
        }),

        sessionsTableRow: function() {
            let tableRows = []
            this.all_sessions.forEach(session => {
                const {
                    id,
                    attributes: { idle, connected, user, remote, connections },
                } = session

                let connectionOfThisServer = connections.find(
                    connection => connection.server === this.current_server.id
                )

                if (connectionOfThisServer) {
                    tableRows.push({
                        id: id,
                        user: `${user}@${remote}`,
                        connected: connected,
                        idle: idle,
                    })
                }
            })
            return tableRows
        },
    },
    async created() {
        while (this.loop) {
            await Promise.all([this.fetchAllSessions(), this.$help.delay(10000)])
        }
    },
    beforeDestroy() {
        this.loop = false
    },
    methods: {
        ...mapActions({
            fetchAllSessions: 'session/fetchAllSessions',
        }),
    },
}
</script>
