<template>
    <div class="d-inline-flex flex-wrap">
        <div class="ml-4" data-test="exe-time">
            <span class="font-weight-bold">{{ $mxs_t('exeTime') }}:</span>
            {{ isGettingEndTime ? 'N/A' : `${executionTime} sec` }}
        </div>
        <div class="ml-4" data-test="latency-time">
            <span class="font-weight-bold">{{ $mxs_t('latency') }}:</span>
            {{ isGettingEndTime ? 'N/A' : `${latency} sec` }}
        </div>
        <div class="ml-4" data-test="total-time">
            <span class="font-weight-bold"> {{ $mxs_t('total') }}:</span>
            {{ isGettingEndTime ? Math.round(duration) : duration }} sec
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
        }
    },
    computed: {
        isGettingEndTime() {
            return this.totalDuration === 0
        },
        latency() {
            return Math.abs(this.duration - this.executionTime).toFixed(4)
        },
    },
    watch: {
        executionTime: {
            immediate: true,
            handler(v) {
                if (v === -1) this.updateSecond()
            },
        },
        totalDuration: {
            immediate: true,
            handler(v) {
                this.duration = v
            },
        },
    },
    methods: {
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
