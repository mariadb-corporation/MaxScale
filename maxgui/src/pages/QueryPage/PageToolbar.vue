<template>
    <div ref="pageToolbar" class="page-toolbar d-flex align-center flex-grow-1">
        <div ref="leftBtns" class="d-flex align-center left-buttons pl-2">
            <v-btn
                :disabled="!Object.keys(sql_conns).length"
                small
                class="float-left add-wke-btn"
                icon
                @click="addWke"
            >
                <v-icon size="18" color="deep-ocean">mdi-plus</v-icon>
            </v-btn>
        </div>
        <v-spacer />
        <div ref="rightBtns" class="d-flex align-center right-buttons pr-2">
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        icon
                        small
                        class="query-setting-btn"
                        v-on="on"
                        @click="queryConfigDialog = !queryConfigDialog"
                    >
                        <v-icon size="16" color="accent-dark">
                            $vuetify.icons.settings
                        </v-icon>
                    </v-btn>
                </template>
                <span class="text-capitalize"> {{ $tc('settings', 2) }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        icon
                        small
                        class="min-max-btn"
                        v-on="on"
                        @click="SET_FULLSCREEN(!is_fullscreen)"
                    >
                        <v-icon size="22" color="accent-dark">
                            mdi-fullscreen{{ is_fullscreen ? '-exit' : '' }}
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ is_fullscreen ? $t('minimize') : $t('maximize') }}</span>
            </v-tooltip>
        </div>

        <query-config-dialog v-model="queryConfigDialog" />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapMutations, mapState } from 'vuex'
import QueryConfigDialog from './QueryConfigDialog'
export default {
    name: 'page-toolbar',
    components: {
        'query-config-dialog': QueryConfigDialog,
    },
    data() {
        return {
            queryConfigDialog: false,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.wke.is_fullscreen,
            sql_conns: state => state.queryConn.sql_conns,
        }),
    },
    mounted() {
        this.$nextTick(() =>
            this.$emit(
                'get-total-btn-width',
                this.$refs.rightBtns.clientWidth + this.$refs.leftBtns.clientWidth
            )
        )
    },
    methods: {
        ...mapMutations({ SET_FULLSCREEN: 'wke/SET_FULLSCREEN' }),
        ...mapActions({ addNewWs: 'wke/addNewWs' }),
        async addWke() {
            await this.addNewWs()
        },
    },
}
</script>
<style lang="scss" scoped>
.page-toolbar {
    border-right: 1px solid $table-border;
    border-top: 1px solid $table-border;
}
</style>
