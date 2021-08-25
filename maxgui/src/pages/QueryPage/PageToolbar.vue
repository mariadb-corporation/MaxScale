<template>
    <div ref="pageToolbar" class="page-toolbar d-flex align-center flex-grow-1 pr-2">
        <v-btn
            :disabled="!cnct_resources.length"
            small
            class="ml-2 float-left"
            icon
            @click="addNewWs"
        >
            <v-icon size="18" color="deep-ocean">add</v-icon>
        </v-btn>
        <v-spacer />
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn icon small v-on="on" @click="queryConfigDialog = !queryConfigDialog">
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
                <v-btn icon small v-on="on" @click="SET_FULLSCREEN(!is_fullscreen)">
                    <v-icon size="20" color="accent-dark">
                        fullscreen{{ is_fullscreen ? '_exit' : '' }}
                    </v-icon>
                </v-btn>
            </template>
            <span>{{ is_fullscreen ? $t('minimize') : $t('maximize') }}</span>
        </v-tooltip>
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
 * Change Date: 2025-08-17
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
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.query.is_fullscreen,
            cnct_resources: state => state.query.cnct_resources,
            worksheets_arr: state => state.query.worksheets_arr,
        }),
    },
    methods: {
        ...mapMutations({
            SET_FULLSCREEN: 'query/SET_FULLSCREEN',
            ADD_NEW_WKE: 'query/ADD_NEW_WKE',
            SET_ACTIVE_WKE_ID: 'query/SET_ACTIVE_WKE_ID',
        }),
        ...mapActions({
            handleDeleteWke: 'query/handleDeleteWke',
        }),
        addNewWs() {
            this.ADD_NEW_WKE()
            this.SET_ACTIVE_WKE_ID(this.worksheets_arr[this.worksheets_arr.length - 1].id)
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
