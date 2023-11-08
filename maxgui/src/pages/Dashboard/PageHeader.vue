<template>
    <div>
        <portal to="page-header">
            <h4
                style="margin-bottom: 0px; line-height: normal;"
                class="mxs-color-helper text-navigation text-h4 text-capitalize"
            >
                {{ pageTitle }}
            </h4>

            <span
                style="position:relative;top:-15px"
                class="grayed-out-info mxs-color-helper text-grayed-out text-capitalize"
            >
                {{ $mxs_t('uptime') }}
                {{ $helpers.uptimeHumanize(uptime) }}
            </span>

            <v-menu
                transition="slide-y-transition"
                :close-on-content-click="false"
                open-on-hover
                offset-y
                nudge-left="20"
                content-class="v-menu--with-arrow v-menu--with-arrow--top-left mt-n2 shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <v-icon
                        class="material-icons-outlined pointer pb-4"
                        style="position:relative;top:-8px"
                        size="16"
                        color="#9DB4BB"
                        v-on="on"
                    >
                        mdi-information-outline
                    </v-icon>
                </template>

                <v-sheet style="border-radius: 10px;" class="px-6 py-6" max-width="320px">
                    <span class="d-block mb-1 text-body-2 font-weight-bold text-capitalize">
                        {{ $mxs_t('aboutMaxScale') }}
                    </span>
                    <div v-for="(value, name) in getMaxScaleInfo" :key="name">
                        <span class="d-flex text-body-2">
                            <span class="text-capitalize" style="width:35%">
                                {{ name.split('_').join(' ') }}
                            </span>
                            <v-tooltip
                                v-if="name === 'commit'"
                                :key="copyState"
                                transition="slide-y-reverse-transition"
                                top
                            >
                                <template v-slot:activator="{ on }">
                                    <div
                                        style="width:65%;"
                                        class="pointer d-inline-block text-truncate"
                                        @dblclick="copyToClipboard(value)"
                                        v-on="on"
                                    >
                                        {{ value }}
                                    </div>
                                </template>
                                {{ copyState }}
                            </v-tooltip>
                            <div
                                v-else-if="
                                    value && (name === 'started_at' || name === 'activated_at')
                                "
                                style="width:65%;"
                                class="d-inline-block "
                            >
                                {{
                                    $helpers.dateFormat({
                                        value,
                                        formatType: 'dd MMM yyyy HH:mm:ss',
                                    })
                                }}
                            </div>
                            <div v-else style="width:65%;" class="d-inline-block ">
                                {{ value }}
                            </div>
                        </span>
                    </div>
                </v-sheet>
            </v-menu>
        </portal>
        <portal to="page-header--right">
            <slot name="refresh-rate" />
            <global-search class="ml-4 d-inline-block" />
            <create-mxs-obj class="ml-4 d-inline-block" :defFormType="defFormType" />
        </portal>
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import workerTimer from 'worker-loader!utils/workerTimer.js'

export default {
    name: 'page-header',

    data() {
        return {
            isCopied: false,
            uptime: null,
            copyState: 'Double click to copy to clipboard',
            workerList: [{ name: 'MaxScale uptime worker timer', interval: 1000 }],
        }
    },
    computed: {
        ...mapState({
            maxscale_overview_info: state => state.maxscale.maxscale_overview_info,
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
        }),
        pageTitle: function() {
            const { version = '' } = this.maxscale_overview_info
            return `MariaDB MaxScale ${version}`
        },
        getMaxScaleInfo: function() {
            const { commit, started_at, activated_at } = this.maxscale_overview_info
            return {
                commit,
                started_at,
                activated_at,
            }
        },
        defFormType() {
            const { SERVERS, SERVICES, LISTENERS, FILTERS } = this.MXS_OBJ_TYPES
            switch (this.$route.name) {
                case SERVERS:
                case SERVICES:
                case LISTENERS:
                case FILTERS:
                    return this.$route.name
                default:
                    return SERVICES
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
        'maxscale_overview_info.uptime'(v) {
            this.uptime = v
            this.workerInit()
        },
    },

    beforeDestroy() {
        this.workerTimer && this.workerTimer.terminate()
    },

    methods: {
        //---------------------- MaxScale overview info
        copyToClipboard(txt) {
            this.$helpers.copyTextToClipboard(txt)
            this.isCopied = true
        },
        updateUpTime() {
            this.uptime = this.uptime + 1
        },

        workerInit() {
            this.workerTimer = new workerTimer()
            this.workerTimer.postMessage(this.workerList)
            this.workerTimer.onmessage = () => {
                this.updateUpTime()
            }
        },
    },
}
</script>
