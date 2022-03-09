<template>
    <div>
        <v-tooltip
            :disabled="isGettingEndTime"
            top
            transition="slide-y-transition"
            content-class="shadow-drop pa-3"
        >
            <template v-slot:activator="{ on }">
                <pre v-on="on">
                    {{ $tc('seconds', duration === 1 ? 1 : 2, { value: duration }) }}</pre
                >
            </template>
            <v-sheet min-width="220" max-width="450" class="color text-small-text">
                <div class="d-flex align-center color text-navigation font-weight-bold">
                    <span>{{ $t('totalDuration') }}:</span>
                    <v-spacer />
                    <span> {{ $tc('seconds', duration === 1 ? 1 : 2, { value: duration }) }}</span>
                </div>
                <div class="d-flex align-center">
                    <span>{{ $t('networkDelay') }} </span>
                    <v-spacer />
                    <span class="color text-navigation">
                        {{
                            $tc('seconds', networkDelay === 1 ? 1 : 2, {
                                value: networkDelay,
                            })
                        }}
                    </span>
                </div>
                <div class="d-flex align-center">
                    <span>{{ $t('exeTime') }}: </span>
                    <v-spacer />
                    <span class="color text-navigation">
                        {{ $tc('seconds', executionTime === 1 ? 1 : 2, { value: executionTime }) }}
                    </span>
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
 * Change Date: 2026-03-08
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
            rmExecTimeWatcher: null,
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
        this.updateSecond()
        this.duration = this.totalDuration
        this.addExeTimeWatcher()
    },
    deactivated() {
        this.rmExecTimeWatcher()
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
                this.duration = parseFloat(currSec)
                requestAnimationFrame(this.updateSecond)
            } else this.duration = this.totalDuration
        },
    },
}
</script>
