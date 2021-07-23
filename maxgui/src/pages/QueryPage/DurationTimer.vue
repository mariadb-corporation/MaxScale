<template>
    <div>
        <v-tooltip
            :disabled="isGettingEndTime"
            top
            transition="slide-y-transition"
            content-class="shadow-drop pa-3"
        >
            <template v-slot:activator="{ on }">
                <pre v-on="on"> {{ totalDuration }} seconds</pre>
            </template>
            <v-sheet min-width="220" max-width="450" class="color text-small-text">
                <div class="d-flex align-center color text-navigation font-weight-bold">
                    <span>Total Duration:</span>
                    <v-spacer />
                    <span>{{ totalDuration }} seconds</span>
                </div>
                <div class="d-flex align-center">
                    <span>Network delay</span>
                    <v-spacer />
                    <span class="color text-navigation">
                        {{ Math.abs(totalDuration - executionTime).toFixed(4) }} seconds
                    </span>
                </div>
                <div class="d-flex align-center">
                    <span>Execution Time:</span>
                    <v-spacer />
                    <span class="color text-navigation"> {{ executionTime }} seconds</span>
                </div>
            </v-sheet>
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
 * Change Date: 2025-07-14
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
    },
    data() {
        return {
            totalDuration: 0,
            rmExecTimeWatcher: null,
        }
    },
    computed: {
        isGettingEndTime() {
            return this.executionTime === -1
        },
    },
    mounted() {
        this.addExeTimeWatcher()
        this.updateSecond()
    },
    deactivated() {
        this.rmExecTimeWatcher()
    },
    activated() {
        this.addExeTimeWatcher()
    },
    methods: {
        addExeTimeWatcher() {
            // store watcher to rmExecTimeWatcher and use it for removing the watcher
            this.rmExecTimeWatcher = this.$watch('executionTime', v => {
                if (v === -1) this.updateSecond()
            })
        },
        updateSecond() {
            const now = new Date().valueOf()
            const currSec = ((now - this.startTime) / 1000).toFixed(4)
            if (this.isGettingEndTime) {
                this.totalDuration = parseFloat(currSec)
                requestAnimationFrame(this.updateSecond)
            }
        },
    },
}
</script>
