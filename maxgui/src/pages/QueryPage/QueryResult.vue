<template>
    <v-tabs v-model="activeTab" class="tab-navigation-wrapper fill-height">
        <v-tab color="primary" :href="`#${SQL_QUERY_MODES.QUERY_VIEW}`">
            <span> Results </span>
        </v-tab>
        <v-tab color="primary" :href="`#${SQL_QUERY_MODES.PRVW_DATA}`">
            <span> Data preview </span>
        </v-tab>
        <v-tabs-items v-model="activeTab" class="tab-items">
            <v-tab-item :value="SQL_QUERY_MODES.QUERY_VIEW" :class="tabItemClass">
                <keep-alive>
                    <result-tab :dynDim="componentDynDim" />
                </keep-alive>
            </v-tab-item>
            <v-tab-item :value="SQL_QUERY_MODES.PRVW_DATA" :class="tabItemClass">
                <keep-alive>
                    <preview-data-tab
                        :dynDim="componentDynDim"
                        :previewDataSchemaId="previewDataSchemaId"
                    />
                </keep-alive>
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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
        previewDataSchemaId: { type: String, require: true },
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
            tabItemClass: 'pt-2 px-5 query-result-fontStyle color text-small-text fill-height',
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
        }),
        componentDynDim() {
            /* Use ref to calculate the dim
             * width: dynDim.width - px-5
             * height: dynDim.height - $tab-bar-height - pt-2 - border thickness
             */
            return { width: this.dynDim.width - 40, height: this.dynDim.height - 24 - 8 - 2 }
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
$tab-bar-height: 24px;
::v-deep.tab-navigation-wrapper {
    .v-tabs-bar {
        height: $tab-bar-height;
    }
}
::v-deep.tab-items {
    height: calc(100% - #{$tab-bar-height});
    .v-window__container {
        height: 100%;
    }
}
</style>
