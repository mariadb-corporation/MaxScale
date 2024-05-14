<template>
    <div
        class="query-tab-nav-toolbar-ctr d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border"
    >
        <div ref="buttonWrapper" class="d-flex align-center px-2">
            <v-btn
                :disabled="$typy(activeQueryTabConn).isEmptyObject"
                small
                class="float-left add-query-tab-btn"
                icon
                @click="$emit('add')"
            >
                <v-icon size="18" color="blue-azure">mdi-plus</v-icon>
            </v-btn>
        </div>
        <div ref="toolbarRight" class="ml-auto d-flex align-center mx-3 fill-height">
            <connection-btn :activeConn="activeQueryTabConn" @click="$emit('edit-conn')" />
            <!-- A slot for SkySQL Query Editor in service details page where the worksheet tab is hidden  -->
            <slot name="query-tab-nav-toolbar-right" />
        </div>
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 Emits:
 - add
 - edit-conn
*/
import ConnectionBtn from '@wkeComps/ConnectionBtn.vue'

export default {
    name: 'query-tab-nav-toolbar',
    components: { ConnectionBtn },
    props: {
        activeQueryTabConn: { type: Object, required: true },
    },
    computed: {
        connectedServerName() {
            return this.$typy(this.activeQueryTabConn, 'meta.name').safeString
        },
    },
    watch: {
        connectedServerName() {
            this.calcWidth()
        },
    },
    mounted() {
        this.calcWidth()
    },
    methods: {
        calcWidth() {
            this.$nextTick(() =>
                this.$emit(
                    'get-total-btn-width',
                    // (24 padding mx-3)
                    this.$refs.buttonWrapper.clientWidth + this.$refs.toolbarRight.clientWidth + 24
                )
            )
        },
    },
}
</script>
