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
            <table-list
                v-if="
                    activeView === SQL_QUERY_MODES.HISTORY ||
                        activeView === SQL_QUERY_MODES.FAVORITE
                "
                :key="activeView"
                :height="dynDim.height - headerHeight"
                :width="dynDim.width"
                :headers="headers"
                :rows="rows"
                @custom-group="customGroup"
            >
                <template v-slot:date="{ data: { cell, maxWidth } }">
                    <truncate-string
                        :text="
                            `${$help.dateFormat({
                                value: cell,
                                formatType:
                                    activeView === SQL_QUERY_MODES.HISTORY
                                        ? 'ddd, DD MMM YYYY'
                                        : 'DATE_RFC2822',
                            })}`
                        "
                        :maxWidth="maxWidth"
                    />
                </template>
            </table-list>
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
import ResultDataTable from './ResultDataTable'
export default {
    name: 'history-and-favorite',
    components: {
        'table-list': ResultDataTable,
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
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
            query_history: state => state.persisted.query_history,
            query_favorite: state => state.persisted.query_favorite,
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
        headers() {
            let data = []
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY:
                    data = this.query_history
                    break
                case this.SQL_QUERY_MODES.FAVORITE:
                    data = this.query_favorite
            }
            return Object.keys(this.$typy(data[0]).safeObjectOrEmpty).map(field => {
                let header = {
                    text: field,
                }
                // assign default width to each column to have better view
                switch (field) {
                    case 'date':
                        header.width = 150
                        header.groupable = true
                        header.hasCustomGroup = true
                        break
                    case 'connection_name':
                        header.width = 215
                        header.groupable = true
                        break
                    case 'time':
                        header.width = 90
                        break
                    case 'execution_time':
                        header.width = 150
                        break
                    case 'sql':
                        header.groupable = true
                        break
                }
                return header
            })
        },
        rows() {
            let data = []
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.HISTORY:
                    data = this.query_history
                    break
                case this.SQL_QUERY_MODES.FAVORITE:
                    data = this.query_favorite
            }
            return data.map(item => Object.values(item))
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
        /** Custom groups 2d array with same value at provided index to a Map
         * @param {Array} data.rows - 2d array to be grouped into a Map
         * @param {Number} data.idx - col index of the inner array
         * @param {Object} data.header - header object
         * @param {Function} callback - Callback function to pass the result
         */
        customGroup(data, callback) {
            const { rows, idx, header } = data
            switch (header.text) {
                case 'date': {
                    let map = new Map()
                    rows.forEach(row => {
                        const key = this.$help.dateFormat({
                            value: row[idx],
                            formatType: 'ddd, DD MMM YYYY',
                        })
                        let matrix = map.get(key) || [] // assign an empty arr if not found
                        matrix.push(row)
                        map.set(key, matrix)
                    })
                    callback(map)
                }
            }
        },
    },
}
</script>
