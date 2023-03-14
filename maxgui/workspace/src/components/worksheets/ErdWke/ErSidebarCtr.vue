<template>
    <!-- TODO: Show tables -->
    <wke-sidebar
        v-model="isCollapsed"
        :title="$mxs_t('tables')"
        disableReload
        @reload="fetchErData"
    >
        <template v-slot:toolbar-append>
            <!-- TODO: Add connection manager -->
        </template>
    </wke-sidebar>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
import WkeSidebar from '@wkeComps/WkeSidebar.vue'
export default {
    name: 'er-sidebar-ctr',
    components: { WkeSidebar },
    computed: {
        ...mapState({
            is_erd_sidebar_collapsed: state => state.prefAndStorage.is_erd_sidebar_collapsed,
        }),
        isCollapsed: {
            get() {
                return this.is_erd_sidebar_collapsed
            },
            set(v) {
                this.SET_IS_ERD_SIDEBAR_COLLAPSED(v)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_ERD_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_ERD_SIDEBAR_COLLAPSED',
        }),
        //TODO: Add action in erdTasks module to fetch tables DDL
        fetchErData() {},
    },
}
</script>
