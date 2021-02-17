<template>
    <collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="title"
        :titleInfo="titleInfo"
    >
        <template v-slot:content>
            <data-table
                :tableClass="tableClass"
                :search="search_keyword"
                :headers="tableHeaders"
                :data="tableRows"
                :loading="isLoading"
                :noDataText="noDataText === '' ? $t('$vuetify.noDataText') : noDataText"
                :tdBorderLeft="tdBorderLeft"
                showAll
                :isTree="isTree"
            >
                <template v-for="(header, i) in tableHeaders" v-slot:[header.value]="cellProps">
                    <slot :name="header.value" :cellProps="cellProps"> </slot>
                </template>
            </data-table>
        </template>
    </collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * If customTableHeaders is not provided, this component renders table
 * with default headers name: Variable and Value
 *
 * It accepts tableData as an object or processed data array with
 * valid data-table format.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState } from 'vuex'

export default {
    name: 'details-readonly-table',
    props: {
        tableClass: { type: String, default: '' },
        title: { type: String, required: true },
        tdBorderLeft: { type: Boolean, default: true },
        titleInfo: { type: [String, Number], default: '' },
        noDataText: { type: String, default: '' },
        tableData: { type: [Object, Array], required: true },
        isTree: { type: Boolean, default: false },
        customTableHeaders: { type: Array },
    },

    data() {
        return {
            showTable: true,
            defaultTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%' },
            ],
            tableRows: [],
            isMounting: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            search_keyword: 'search_keyword',
        }),
        tableHeaders: function() {
            return this.customTableHeaders ? this.customTableHeaders : this.defaultTableHeaders
        },
        isLoading: function() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
    },
    watch: {
        tableData: {
            handler: function(val, oldVal) {
                if (!this.$help.lodash.isEqual(val, oldVal)) {
                    this.processTableRows(val)
                }
            },
            deep: true,
        },
    },
    async mounted() {
        this.processTableRows(this.tableData)
        await this.$help.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        /**
         * This converts tableData to array if it is an object according to data-table data
         * array format.
         */
        processTableRows(data) {
            if (Array.isArray(data)) {
                this.tableRows = data
            } else {
                this.tableRows = this.$help.objToTree({
                    obj: data,
                    keepPrimitiveValue: true,
                    level: 0,
                })
            }
        },
    },
}
</script>
