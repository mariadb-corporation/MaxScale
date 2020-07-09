<template>
    <v-col class="py-0 ma-0" cols="8">
        <collapse
            :toggleOnClick="() => (showSessions = !showSessions)"
            :isContentVisible="showSessions"
            :title="`${$tc('currentSessions', 2)}`"
            :titleInfo="sessionsTableRow.length"
        >
            <template v-slot:content>
                <data-table
                    tableClass="data-table-full--max-width-columns"
                    :search="searchKeyWord"
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
    </v-col>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapActions } from 'vuex'

export default {
    name: 'session-table',
    props: {
        searchKeyWord: { type: String, required: true },
        loading: { type: Boolean, required: true },
        currentServer: { type: Object, required: true },
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
        }
    },
    computed: {
        ...mapGetters({ allSessions: 'session/allSessions' }),
        sessionsTableRow: function() {
            if (this.allSessions.length) {
                let self = this
                let itemsArr = []
                for (let i = 0; i < self.allSessions.length; ++i) {
                    /**
                     * @typedef {Object} row
                     * @property {Number} row.id - sessions's id
                     * @property {String} row.user - sessions's user
                     * @property {String} row.connected - sessions's sessions
                     * @property {Number} row.idle - idle (seconds)
                     */
                    const {
                        id,
                        attributes: { idle, connected, user, remote, connections },
                    } = self.allSessions[i]

                    let connectionOfThisServer = connections.find(
                        connection => connection.server === self.currentServer.id
                    )

                    if (connectionOfThisServer) {
                        let row = {
                            id: id,
                            user: `${user}@${remote}`,
                            connected: connected,
                            idle: idle,
                        }
                        itemsArr.push(row)
                    }
                }
                return itemsArr
            }
            return []
        },
    },
    async created() {
        await this.fetchSessionsLoop()
    },
    methods: {
        ...mapActions({
            fetchAllSessions: 'session/fetchAllSessions',
        }),
        async fetchSessionsLoop() {
            await this.fetchAllSessions()
            await setTimeout(() => {
                this.fetchSessionsLoop()
            }, 10000)
        },
    },
}
</script>
