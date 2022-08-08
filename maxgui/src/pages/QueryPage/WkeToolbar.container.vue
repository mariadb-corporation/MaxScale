<template>
    <wke-toolbar
        :isFullscreen="is_fullscreen"
        :isAddWkeDisabled="!Object.keys(sql_conns).length"
        @on-add-wke="addNewWs"
        @on-fullscreen-click="SET_FULLSCREEN(!is_fullscreen)"
        v-on="$listeners"
    />
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapMutations, mapState } from 'vuex'
import WkeToolbar from './WkeToolbar'

export default {
    name: 'wke-toolbar-ctr',
    components: {
        'wke-toolbar': WkeToolbar,
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.wke.is_fullscreen,
            sql_conns: state => state.queryConn.sql_conns,
        }),
    },
    methods: {
        ...mapMutations({ SET_FULLSCREEN: 'wke/SET_FULLSCREEN' }),
        ...mapActions({ addNewWs: 'wke/addNewWs' }),
    },
}
</script>
