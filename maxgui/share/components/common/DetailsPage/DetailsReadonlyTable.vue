<template>
    <mxs-collapse :title="title" :titleInfo="titleInfo">
        <data-table
            :tableClass="tableClass"
            :search="search_keyword"
            :headers="tableHeaders"
            :data="tableRows"
            :loading="isLoading"
            :noDataText="noDataText === '' ? $mxs_t('$vuetify.noDataText') : noDataText"
            :tdBorderLeft="tdBorderLeft"
            showAll
            :expandAll="expandAll"
            :isTree="isTree"
        />
    </mxs-collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * It accepts tableData as an object or processed data array with
 * valid data-table format.
 */
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import { mapState } from 'vuex'
import { objToTree } from '@rootSrc/utils/dataTableHelpers'

export default {
    name: 'details-readonly-table',
    props: {
        tableClass: { type: String, default: '' },
        title: { type: String, required: true },
        tdBorderLeft: { type: Boolean, default: true },
        titleInfo: { type: [String, Number], default: '' },
        noDataText: { type: String, default: '' },
        tableData: { type: [Object, Array], required: true },
        expandAll: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
        isLoadingData: { type: Boolean, default: false },
    },

    data() {
        return {
            tableHeaders: [
                { text: 'Variable', value: 'id', width: '65%', autoTruncate: true },
                { text: 'Value', value: 'value', width: '35%', autoTruncate: true },
            ],
            tableRows: [],
            isMounting: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: state => state.mxsApp.overlay_type,
            search_keyword: 'search_keyword',
        }),
        isLoading() {
            return (
                this.isLoadingData ||
                this.isMounting ||
                this.overlay_type === OVERLAY_TRANSPARENT_LOADING
            )
        },
    },
    watch: {
        tableData: {
            handler: function(val, oldVal) {
                if (!this.$helpers.lodash.isEqual(val, oldVal)) {
                    this.processTableRows(val)
                }
            },
            deep: true,
        },
    },
    async mounted() {
        this.processTableRows(this.tableData)
        await this.$helpers.delay(400).then(() => (this.isMounting = false))
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
                this.tableRows = objToTree({
                    obj: data,
                    keepPrimitiveValue: true,
                    level: 0,
                })
            }
        },
    },
}
</script>
