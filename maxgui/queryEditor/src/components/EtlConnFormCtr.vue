<template>
    <v-form
        v-bind="{ ...$attrs }"
        lazy-validation
        class="fill-height d-flex flex-column"
        v-on="$listeners"
    >
        <v-container class="fill-height pa-0">
            <v-row class="fill-height">
                <etl-src-conn v-model="src" :drivers="odbc_drivers" />
                <etl-dest-conn
                    v-model="dest"
                    :allServers="allServers"
                    :destTargetType="destTargetType"
                />
            </v-row>
        </v-container>
    </v-form>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlSrcConn from './EtlSrcConn.vue'
import EtlDestConn from './EtlDestConn.vue'
import { mapActions, mapState } from 'vuex'

export default {
    name: 'etl-conn-form-ctr',
    components: { EtlSrcConn, EtlDestConn },
    inheritAttrs: false,
    data() {
        return {
            src: { driver: '', server: '', port: '', user: '', password: '', db: '' },
            dest: { user: '', password: '', db: '', target: '' },
        }
    },
    computed: {
        ...mapState({
            odbc_drivers: state => state.queryConnsMem.odbc_drivers,
            rc_target_names_map: state => state.queryConnsMem.rc_target_names_map,
        }),
        destTargetType() {
            return 'servers'
        },
        allServers() {
            return this.rc_target_names_map[this.destTargetType] || []
        },
    },
    async created() {
        await this.fetchOdbcDrivers()
        await this.fetchRcTargetNames(this.destTargetType)
    },
    methods: {
        ...mapActions({
            fetchOdbcDrivers: 'queryConnsMem/fetchOdbcDrivers',
            fetchRcTargetNames: 'queryConnsMem/fetchRcTargetNames',
        }),
    },
}
</script>
