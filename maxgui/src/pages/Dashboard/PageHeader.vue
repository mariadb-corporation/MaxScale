<template>
    <div>
        <portal to="page-header">
            <h4
                style="margin-bottom: 0px; line-height: normal;"
                class="color text-navigation text-navigation display-1 text-capitalize"
            >
                {{ pageTitle }}
            </h4>

            <span
                style="position:relative;top:-15px"
                class="field-text-info color text-field-text text-capitalize"
            >
                {{ $t('uptime') }}
                {{
                    [uptime, 'seconds']
                        | duration('format', 'Y [years] M [months] D [days] h:mm:ss', {
                            stopTrim: 'h mm',
                        })
                }}
            </span>

            <v-menu
                transition="slide-y-transition"
                :close-on-content-click="false"
                open-on-hover
                offset-y
                nudge-left="20"
                content-class="v-menu--with-arrow v-menu--with-arrow--top-left shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <v-icon
                        class="material-icons-outlined pointer"
                        style="position:relative;top:-15px"
                        size="16"
                        color="#9DB4BB"
                        v-on="on"
                    >
                        info
                    </v-icon>
                </template>

                <v-sheet style="border-radius: 10px;" class="px-6 py-6" max-width="320px">
                    <span class="d-block mb-1 body-2 font-weight-bold text-capitalize">
                        {{ $t('aboutMaxScale') }}
                    </span>
                    <div v-for="(value, name) in getMaxScaleInfo" :key="name">
                        <span class="d-flex body-2">
                            <span class="text-capitalize" style="width:35%">
                                {{ name.split('_').join(' ') }}
                            </span>
                            <v-tooltip
                                v-if="name === 'commit'"
                                :key="copyState"
                                transition="slide-y-reverse-transition"
                                top
                                content-class="shadow-drop color text-navigation py-1 px-4"
                            >
                                <template v-slot:activator="{ on }">
                                    <div
                                        style="width:65%;"
                                        class="pointer d-inline-block text-truncate"
                                        @dblclick="copyToClipboard()"
                                        v-on="on"
                                    >
                                        {{ value }}
                                    </div>
                                </template>
                                <span>
                                    {{ copyState }}
                                </span>
                            </v-tooltip>
                            <div
                                v-else-if="
                                    value && (name === 'started_at' || name === 'activated_at')
                                "
                                style="width:65%;"
                                class="d-inline-block "
                            >
                                {{ $help.formatValue(value, 'MM.DD.YYYY HH:mm:ss') }}
                            </div>
                            <div v-else style="width:65%;" class="d-inline-block ">
                                {{ value }}
                            </div>
                        </span>
                    </div>
                </v-sheet>
            </v-menu>
        </portal>
        <portal to="page-search">
            <global-search />
        </portal>
        <portal to="create-resource">
            <create-resource />
        </portal>
    </div>
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
import { mapGetters } from 'vuex'
import workerTimer from 'worker-loader!utils/workerTimer.js'

export default {
    name: 'page-title',

    data() {
        return {
            isCopied: false,
            uptime: null,
            copyState: 'Double click to copy to clipboard',
            workerList: [{ name: 'MaxScale uptime worker timer', interval: 1000 }],
        }
    },
    computed: {
        ...mapGetters({
            maxScaleOverviewInfo: 'maxscale/maxScaleOverviewInfo',
        }),
        pageTitle: function() {
            let version =
                this.maxScaleOverviewInfo.version !== undefined
                    ? this.maxScaleOverviewInfo.version
                    : ''
            return `MariaDB ${this.$t('productName')} ${version}`
        },
        getMaxScaleInfo: function() {
            const { commit, started_at, activated_at } = this.maxScaleOverviewInfo
            return {
                commit,
                started_at,
                activated_at,
            }
        },
    },
    watch: {
        isCopied: function(newVal) {
            if (newVal) {
                let self = this
                self.copyState = 'Copied'
                setTimeout(
                    () => (
                        (self.isCopied = false),
                        (self.copyState = 'Double click to copy to clipboard')
                    ),
                    2000
                )
            }
        },
        'maxScaleOverviewInfo.uptime': function(val) {
            let self = this
            self.uptime = val //548888888
            self.workerInit()
        },
    },

    beforeDestroy() {
        this.workerTimer && this.workerTimer.terminate()
    },

    methods: {
        //---------------------- MaxScale overview info
        copyToClipboard() {
            document.execCommand('copy')
            this.isCopied = true
        },
        updateUpTime() {
            this.uptime = this.uptime + 1
        },

        workerInit() {
            let self = this
            self.workerTimer = new workerTimer()
            self.workerTimer.postMessage(self.workerList)
            self.workerTimer.onmessage = () => {
                self.updateUpTime()
            }
        },
    },
}
</script>
