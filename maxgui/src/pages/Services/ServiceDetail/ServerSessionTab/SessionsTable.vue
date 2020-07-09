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
                    :search="searchKeyWord"
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

export default {
    name: 'sessions-table',
    props: {
        searchKeyWord: { type: String, required: true },
        loading: { type: Boolean, required: true },
        sessionsByService: { type: Array, required: true },
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
        sessionsTableRow: function() {
            if (this.sessionsByService.length) {
                let itemsArr = []
                let allSessions = this.$help.lodash.cloneDeep(this.sessionsByService)
                for (let n = allSessions.length - 1; n >= 0; --n) {
                    /**
                     * @typedef {Object} row
                     * @property {Number} row.id - sessions's id
                     * @property {String} row.user - sessions's user
                     * @property {String} row.connected - sessions's sessions
                     * @property {Number} row.idle - idle (seconds)
                     */
                    const {
                        id,
                        attributes: { idle, connected, user, remote },
                    } = allSessions[n] || {}

                    let row = {
                        id: id,
                        user: `${user}@${remote}`,
                        connected: connected,
                        idle: idle,
                    }
                    itemsArr.push(row)
                }
                return itemsArr
            }
            return []
        },
    },
}
</script>
