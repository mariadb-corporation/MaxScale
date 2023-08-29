<template>
    <div ref="wrapper" class="d-flex align-center left-buttons fill-height">
        <v-btn
            :disabled="isAddWkeDisabled"
            small
            class="float-left add-wke-btn"
            icon
            @click="addNewWs"
        >
            <v-icon size="18" color="deep-ocean">mdi-plus</v-icon>
        </v-btn>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Emits
 * $emit('get-total-width', v:number)
 */
import { mapActions, mapState } from 'vuex'
export default {
    name: 'wke-toolbar-left-ctr',
    computed: {
        ...mapState({ sql_conns: state => state.queryConn.sql_conns }),
        isAddWkeDisabled() {
            return !Object.keys(this.sql_conns).length
        },
    },
    mounted() {
        this.$nextTick(() => this.$emit('get-total-width', this.$refs.wrapper.clientWidth))
    },
    methods: {
        ...mapActions({ addNewWs: 'wke/addNewWs' }),
    },
}
</script>
