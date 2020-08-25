<template>
    <collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="title"
    >
        <template v-slot:content>
            <data-table
                :search="search_keyword"
                :headers="tableHeaders"
                :data="tableRows"
                :loading="isLoading"
                tdBorderLeft
                showAll
                :isTree="isTree"
            />
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
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * This component converts objData to array according to data-table data
 * array format.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState } from 'vuex'

export default {
    name: 'details-readonly-table',
    props: {
        title: { type: String, required: true },
        objData: { type: Object, required: true },
        isTree: { type: Boolean, default: false },
    },

    data() {
        return {
            showTable: true,
            tableHeaders: [
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
        isLoading: function() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
    },
    watch: {
        objData: {
            handler: function(val, oldVal) {
                if (!this.$help.lodash.isEqual(val, oldVal)) {
                    this.processTableRows(val)
                }
            },
            deep: true,
        },
    },
    async mounted() {
        this.processTableRows(this.objData)
        await this.$help.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        processTableRows(obj) {
            this.tableRows = this.$help.objToArrOfNodes({
                obj,
                keepPrimitiveValue: true,
                level: 0,
            })
        },
    },
}
</script>
