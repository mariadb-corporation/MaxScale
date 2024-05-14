<template>
    <v-tooltip
        v-if="isActivated"
        :disabled="isGettingEndTime"
        top
        transition="slide-y-transition"
        min-width="250"
        max-width="450"
    >
        <template v-slot:activator="{ on }">
            <span v-on="on">
                {{
                    $mxs_tc('seconds', duration === 1 ? 1 : 2, {
                        value: isGettingEndTime ? Math.round(duration) : duration,
                    })
                }}
            </span>
        </template>
        <table class="duration-table">
            <tr class="font-weight-bold">
                <td>{{ $mxs_t('totalDuration') }}:</td>
                <td>
                    {{ $mxs_tc('seconds', duration === 1 ? 1 : 2, { value: duration }) }}
                </td>
            </tr>
            <tr>
                <td>{{ $mxs_t('networkDelay') }}:</td>
                <td>
                    {{
                        $mxs_tc('seconds', networkDelay === 1 ? 1 : 2, {
                            value: networkDelay,
                        })
                    }}
                </td>
            </tr>
            <tr>
                <td>{{ $mxs_t('exeTime') }}:</td>
                <td>
                    {{ $mxs_tc('seconds', executionTime === 1 ? 1 : 2, { value: executionTime }) }}
                </td>
            </tr>
        </table>
    </v-tooltip>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'duration-timer',
    props: {
        executionTime: {
            type: Number, // in seconds
            required: true,
        },
        startTime: {
            type: Number, // in ms
            required: true,
        },
        totalDuration: {
            type: Number, // in ms
            required: true,
        },
    },
    data() {
        return {
            duration: 0,
            isActivated: false,
        }
    },
    computed: {
        isGettingEndTime() {
            return this.totalDuration === 0
        },
        networkDelay() {
            return Math.abs(this.duration - this.executionTime).toFixed(4)
        },
    },
    activated() {
        this.isActivated = true
        this.duration = this.totalDuration
        this.watch_executionTime()
    },
    deactivated() {
        this.$typy(this.unwatch_executionTime).safeFunction()
        this.isActivated = false
    },
    methods: {
        watch_executionTime() {
            // store watcher to unwatch_executionTime and use it for removing the watcher
            this.unwatch_executionTime = this.$watch(
                'executionTime',
                v => {
                    if (v === -1) this.updateSecond()
                },
                { immediate: true }
            )
        },
        updateSecond() {
            const now = new Date().valueOf()
            const currSec = ((now - this.startTime) / 1000).toFixed(4)
            if (this.isGettingEndTime) {
                this.duration = parseFloat(currSec)
                requestAnimationFrame(this.updateSecond)
            } else this.duration = this.totalDuration
        },
    },
}
</script>

<style lang="scss" scoped>
.duration-table {
    td:first-child {
        padding-right: 8px;
    }
}
</style>
