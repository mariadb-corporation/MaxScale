<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <v-tabs v-model="activeView" hide-slider :height="20" class="tab-navigation--btn-style">
                <v-tab
                    :key="SQL_QUERY_MODES.HISTORY"
                    :href="`#${SQL_QUERY_MODES.HISTORY}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $t('history') }}
                </v-tab>
                <v-tab
                    :key="SQL_QUERY_MODES.FAVORITE"
                    :href="`#${SQL_QUERY_MODES.FAVORITE}`"
                    class="tab-btn px-3 text-uppercase"
                    active-class="tab-btn--active font-weight-medium"
                >
                    {{ $t('favorite') }}
                </v-tab>
            </v-tabs>
        </div>
        <keep-alive>
            <!-- TODO:Showing history/favorite tables here  -->
            <div :style="{ height: `${dynDim.height - headerHeight}px` }"></div>
        </keep-alive>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
export default {
    name: 'history-and-favorite',
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
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
        }),
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (
                    this.curr_query_mode === this.SQL_QUERY_MODES.HISTORY ||
                    this.curr_query_mode === this.SQL_QUERY_MODES.FAVORITE
                )
                    this.SET_CURR_QUERY_MODE(value)
            },
        },
    },
    activated() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
