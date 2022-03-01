<template>
    <v-menu
        transition="slide-y-transition"
        offset-y
        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
        :max-width="200"
    >
        <template v-slot:activator="{ on: menu, attrs }">
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
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
                            $vuetify.icons.reload
                        </v-icon>
                        <span class="mr-1 color text-navigation">
                            {{ $t('refreshRate') }}:
                            <template v-if="refreshRate >= 0">
                                {{
                                    $tc('seconds', refreshRate === 1 ? 1 : 2, {
                                        value: refreshRate,
                                    })
                                }}
                            </template>
                            <template v-else>{{ $t('noRefresh') }} </template>
                        </span>
                    </v-btn>
                </template>
                <span>{{ $t('info.refreshRate') }}</span>
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
                <v-list-item-title class="color text-text">
                    {{ item.label }}
                </v-list-item-title>
            </v-list-item>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@on-count-done. Emit event after amount of time
*/
import workerTimer from 'worker-loader!utils/workerTimer.js'
import asyncEmit from 'mixins/asyncEmit'
export default {
    name: 'refresh-rate',
    mixins: [asyncEmit],
    props: {
        defRefreshRate: { type: Number, default: 10 },
    },
    data() {
        return {
            refreshRate: null,
        }
    },
    computed: {
        refreshRateOpts() {
            return [
                { label: this.$tc('seconds', 2, { value: 10 }), value: 10 },
                { label: this.$tc('seconds', 2, { value: 20 }), value: 20 },
                { label: this.$tc('seconds', 2, { value: 30 }), value: 30 },
                { label: this.$tc('seconds', 2, { value: 60 }), value: 60 },
                { label: this.$tc('seconds', 2, { value: 120 }), value: 120 },
                { label: this.$tc('seconds', 2, { value: 300 }), value: 300 },
                { label: this.$t('noRefresh'), value: -1 },
            ]
        },
    },
    mounted() {
        this.refreshRate = this.defRefreshRate
        this.storeRefreshRateTmp()
        this.workerInit()
    },
    beforeDestroy() {
        this.destroyTimer()
    },
    methods: {
        destroyTimer() {
            this.workerTimer && this.workerTimer.terminate()
        },
        async updateCountTime() {
            if (this.refreshRate > 0) this.refreshRate = this.refreshRate - 1
            else if (this.refreshRate === 0) {
                await this.asyncEmit('on-count-done')
                this.refreshRate = this.refreshRateTmp
            }
        },
        workerInit() {
            this.workerTimer = new workerTimer()
            this.workerTimer.postMessage([{ name: 'Refresh rate timer', interval: 1000 }])
            this.workerTimer.onmessage = async () => {
                await this.updateCountTime()
            }
        },
        storeRefreshRateTmp() {
            this.refreshRateTmp = this.refreshRate
        },
        onSelectRefreshRate(item) {
            this.refreshRate = item.value
            this.storeRefreshRateTmp()
        },
    },
}
</script>

<style lang="scss" scoped>
.refresh-rate-label {
    font-size: 0.875rem;
}
</style>
