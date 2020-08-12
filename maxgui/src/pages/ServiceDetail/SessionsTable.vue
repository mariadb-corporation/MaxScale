<template>
    <collapse
        :toggleOnClick="() => (showSessions = !showSessions)"
        :isContentVisible="showSessions"
        :title="`${$tc('currentSessions', 2)}`"
        :titleInfo="sessionsTableRow.length"
    >
        <template v-slot:content>
            <data-table
                :search="search_keyword"
                tableClass="data-table-full--max-width-columns"
                :headers="sessionsTableHeader"
                :data="sessionsTableRow"
                :sortDesc="false"
                :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
                :loading="loading"
            >
                <!-- <template v-slot:user="{ data: { item: { user } } }">
                        <router-link :key="user" :to="`/users/${user}`" class="no-underline">
                            <span> {{ user }} </span>
                        </router-link>
                    </template> -->
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
import { mapState } from 'vuex'

export default {
    name: 'sessions-table',
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
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            sessions_by_service: state => state.session.sessions_by_service,
        }),

        sessionsTableRow: function() {
            return this.sessions_by_service.map(
                ({ id, attributes: { idle, connected, user, remote } }) => ({
                    id,
                    user: `${user}@${remote}`,
                    connected,
                    idle,
                })
            )
        },
    },
}
</script>
