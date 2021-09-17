<template>
    <v-menu
        top
        offset-y
        transition="slide-y-transition"
        :close-on-content-click="false"
        open-on-hover
        content-class="shadow-drop color text-navigation"
        allow-overflow
        :max-height="350"
        :open-delay="openDelay"
        :disabled="!(showRepStats || showSlaveStats)"
    >
        <template v-slot:activator="{ on }">
            <slot name="activator" :on="on" />
        </template>
        <v-sheet class="py-4 px-3 text-body-2">
            <div class="px-1 py-1 font-weight-bold ">
                {{ showSlaveStats ? $t('slaveRepStatus') : $t('replicationStatus') }}
            </div>
            <v-divider class="color border-separator" />

            <template v-if="showSlaveStats">
                <table class="rep-table px-1">
                    <tr
                        v-for="(slaveStat, i) in getSlaveStatus(serverId)"
                        :key="`${i}`"
                        class="mb-1"
                    >
                        <td>
                            <icon-sprite-sheet
                                size="13"
                                class="mr-1 rep-icon"
                                :frame="$help.repStateIcon(slaveStat.overall_replication_state)"
                            >
                                status
                            </icon-sprite-sheet>
                        </td>
                        <td>
                            <div class="d-flex align-center fill-height">
                                <truncate-string
                                    wrap
                                    :text="slaveStat.id"
                                    :nudgeTop="10"
                                    :maxWidth="300"
                                />
                                <span class="ml-1 color text-field-text">
                                    (+{{ slaveStat.overall_seconds_behind_master }}s)
                                </span>
                            </div>
                        </td>
                    </tr>
                </table>
            </template>

            <table v-else class="rep-table px-1">
                <template v-for="(stat, i) in getRepStats(serverId)">
                    <tbody :key="`${i}`" :class="{ 'tbody-src-replication': !showSlaveStats }">
                        <tr v-for="(value, key) in stat" :key="`${key}`">
                            <td class="pr-5">
                                {{ key }}
                            </td>
                            <td>
                                <div class="d-flex align-center fill-height">
                                    <icon-sprite-sheet
                                        v-if="key === 'replication_state'"
                                        size="13"
                                        class="mr-1 rep-icon"
                                        :frame="$help.repStateIcon(value)"
                                    >
                                        status
                                    </icon-sprite-sheet>
                                    <truncate-string
                                        wrap
                                        :text="`${value}`"
                                        :maxWidth="400"
                                        :nudgeTop="10"
                                    />
                                </div>
                            </td>
                        </tr>
                    </tbody>
                </template>
            </table>
        </v-sheet>
    </v-menu>
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
export default {
    name: 'rep-tooltip',
    props: {
        slaveConnectionsMap: { type: Map, required: true },
        slaveServersByMasterMap: { type: Map, required: true },
        showRepStats: { type: Boolean, default: false },
        showSlaveStats: { type: Boolean, required: false },
        serverId: { type: String, required: false },
        openDelay: { type: Number, default: 0 },
    },
    methods: {
        getRepStats(serverId) {
            const slave_connections = this.slaveConnectionsMap.get(serverId) || []
            if (!slave_connections.length) return []

            const repStats = []
            slave_connections.forEach(slave_conn => {
                const {
                    seconds_behind_master,
                    slave_io_running,
                    slave_sql_running,
                    last_io_error,
                    last_sql_error,
                    connection_name,
                } = slave_conn
                let srcRep = {}
                // show connection_name only when multi-source replication is in use
                if (slave_connections.length > 1) srcRep.connection_name = connection_name

                // Determine replication_state (Stopped||Running||Lagging)
                if (slave_io_running === 'No' || slave_sql_running === 'No')
                    srcRep.replication_state = 'Stopped'
                else if (seconds_behind_master === 0) {
                    if (slave_sql_running === 'Yes' && slave_io_running === 'Yes')
                        srcRep.replication_state = 'Running'
                    else {
                        // use value of either slave_io_running or slave_sql_running
                        srcRep.replication_state =
                            slave_io_running !== 'Yes' ? slave_io_running : slave_sql_running
                    }
                } else srcRep.replication_state = 'Lagging'
                srcRep.server_id = serverId
                // only show last_io_error and last_sql_error when replication_state === 'Stopped'
                if (srcRep.replication_state === 'Stopped')
                    srcRep = {
                        ...srcRep,
                        last_io_error,
                        last_sql_error,
                    }
                srcRep = {
                    ...srcRep,
                    seconds_behind_master,
                    slave_io_running,
                    slave_sql_running,
                }
                repStats.push(srcRep)
            })

            return repStats
        },
        /**
         * This returns maximum value or the most frequent value
         * @param {Array} payload.repStats - replication status get from getRepStats method
         * @param {String} payload.pickBy - property to count. e.g. replication_state or seconds_behind_master
         * @param {Boolean} payload.isNumber - If it is true, returns maximum value instead of the most frequent value
         * @returns {String|Number} - returns maximum value or the most frequent value
         */
        getOverallRepStat({ repStats, pickBy, isNumber }) {
            if (isNumber) return Math.max(...repStats.map(item => item[pickBy]))
            let countObj = this.$help.lodash.countBy(repStats, pickBy)
            return Object.keys(countObj).reduce((a, b) => (countObj[a] > countObj[b] ? a : b))
        },
        getSlaveStatus(serverId) {
            const slaveServerIds = this.slaveServersByMasterMap.get(serverId) || []
            if (!slaveServerIds.length) return []
            const slaveStats = []
            slaveServerIds.forEach(id => {
                const repStats = this.getRepStats(id)
                slaveStats.push({
                    id,
                    overall_replication_state: this.getOverallRepStat({
                        repStats,
                        pickBy: 'replication_state',
                    }),
                    overall_seconds_behind_master: this.getOverallRepStat({
                        repStats,
                        pickBy: 'seconds_behind_master',
                        isNumber: true,
                    }),
                })
            })
            return slaveStats
        },
    },
}
</script>

<style lang="scss" scoped>
.tbody-src-replication {
    &:not(:last-of-type) {
        &::after,
        &:first-of-type::before {
            content: '';
            display: block;
            height: 12px;
        }
    }
}
.rep-table {
    td {
        white-space: nowrap;
        height: 24px;
        line-height: 1.5;
    }
}
</style>
