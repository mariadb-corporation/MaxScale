<template>
    <v-menu
        transition="slide-y-transition"
        offset-y
        content-class="v-menu--mariadb v-menu--mariadb-full-border"
        :max-width="200"
    >
        <template v-slot:activator="{ on: menu, attrs }">
            <v-tooltip top transition="slide-y-transition">
                <template v-slot:activator="{ on: tooltip }">
                    <v-btn
                        class="text-none font-weight-regular refresh-rate-btn px-2"
                        text
                        small
                        v-bind="attrs"
                        color="primary"
                        v-on="{ ...tooltip, ...menu }"
                    >
                        <v-icon class="mr-1" size="12" color="primary">
                            $vuetify.icons.mxs_reload
                        </v-icon>
                        <span class="mr-1 mxs-color-helper text-navigation">
                            {{ $mxs_t('nextRefresh') }}:
                            <template v-if="refreshCount >= 0">
                                {{
                                    $mxs_tc('seconds', refreshCount === 1 ? 1 : 2, {
                                        value: refreshCount,
                                    })
                                }}
                            </template>
                            <template v-else>{{ $mxs_t('noRefresh') }} </template>
                        </span>
                    </v-btn>
                </template>
                {{ $mxs_t('info.nextRefresh') }}
            </v-tooltip>
        </template>
        <v-list>
            <v-list-item
                v-for="item in refreshRateOpts"
                :key="item.value"
                dense
                link
                @click="onSelectRefreshRate(item)"
            >
                <v-list-item-title class="mxs-color-helper text-text">
                    {{ item.label }}
                </v-list-item-title>
            </v-list-item>
        </v-list>
    </v-menu>
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
/*
@on-count-done. Emit event after amount of time
*/
import workerTimer from 'worker-loader!utils/workerTimer.js'
import asyncEmit from '@share/mixins/asyncEmit'
export default {
    name: 'refresh-rate',
    mixins: [asyncEmit],
    props: {
        value: { type: Number, required: true },
    },
    data() {
        return {
            refreshCount: 10,
            chosenRefreshRate: 10,
            seconds: [5, 10, 20, 30, 60, 120, 300],
            paused: false,
        }
    },
    computed: {
        refreshRate: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        refreshRateOpts() {
            return [
                ...this.seconds.map(s => ({
                    label: this.$mxs_tc('seconds', 2, { value: s }),
                    value: s,
                })),
                { label: this.$mxs_t('noRefresh'), value: -1 },
            ]
        },
    },
    watch: {
        refreshRate() {
            this.refreshCount = this.refreshRate
        },
        async refreshCount(v) {
            if (v === 0) {
                // pause until finish the handler for asyncEmit event
                this.paused = true
                await this.asyncEmit('on-count-done')
                this.paused = false
            }
        },
    },
    mounted() {
        this.refreshCount = this.refreshRate
        this.workerInit()
    },
    beforeDestroy() {
        this.destroyTimer()
    },
    methods: {
        destroyTimer() {
            this.workerTimer && this.workerTimer.terminate()
        },
        updateCountTime() {
            if (this.refreshCount > 0) this.refreshCount = this.refreshCount - 1
            else if (this.refreshCount === 0) this.refreshCount = this.refreshRate
        },
        workerInit() {
            this.workerTimer = new workerTimer()
            this.workerTimer.postMessage([{ name: 'Next refresh timer', interval: 1000 }])
            this.workerTimer.onmessage = () => {
                if (!this.paused) this.updateCountTime()
            }
        },
        onSelectRefreshRate(item) {
            this.refreshRate = item.value
            this.refreshCount = this.refreshRate
        },
    },
}
</script>

<style lang="scss" scoped>
.refresh-rate-label {
    font-size: 0.875rem;
}
</style>
