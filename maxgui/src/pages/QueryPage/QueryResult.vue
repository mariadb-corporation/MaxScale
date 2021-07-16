<template>
    <!-- TODO:  refactor it and its child components to use vuex worksheet state-->
    <div class="fill-height">
        <v-tabs v-model="activeTab" :height="24" class="tab-navigation-wrapper">
            <v-tab color="primary" :href="`#${SQL_QUERY_MODES.QUERY_VIEW}`">
                <span> {{ $t('results') }} </span>
            </v-tab>
            <v-tab color="primary" :href="`#${SQL_QUERY_MODES.PRVW_DATA}`">
                <span>{{ $t('dataPrvw') }} </span>
            </v-tab>
        </v-tabs>

        <!-- TODO: use vanila vuejs transtion to have similar reverse v-tab-item transtion -->
        <v-slide-x-transition>
            <keep-alive>
                <result-tab
                    v-if="activeTab === SQL_QUERY_MODES.QUERY_VIEW"
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                />
                <preview-data-tab
                    v-else
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                />
            </keep-alive>
        </v-slide-x-transition>
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
import { mapState, mapMutations } from 'vuex'
import PreviewDataTab from './PreviewDataTab'
import ResultTab from './ResultTab'
export default {
    name: 'query-result',
    components: {
        PreviewDataTab,
        ResultTab,
    },
    props: {
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
    },
    data() {
        return {
            tabItemClass: 'pt-2 px-5 query-result-fontStyle color text-small-text',
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
        }),
        componentDynDim() {
            /*
             * width: dynDim.width - px-5
             * height: dynDim.height - $tab-bar-height - pt-2
             */
            return { width: this.dynDim.width - 40, height: this.dynDim.height - 24 - 8 }
        },
        activeTab: {
            get() {
                /* There are only two tab mode in this component. So PRVW_DATA_DETAILS will be
                 * equal to PRVW_DATA
                 */
                switch (this.curr_query_mode) {
                    case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    case this.SQL_QUERY_MODES.PRVW_DATA:
                        return this.SQL_QUERY_MODES.PRVW_DATA
                    default:
                        return this.curr_query_mode
                }
            },
            set(value) {
                this.SET_CURR_QUERY_MODE(value)
            },
        },
    },
    methods: {
        ...mapMutations({ SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE' }),
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
</style>
