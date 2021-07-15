<template>
    <div class="fill-height worksheet-wrapper">
        <v-tabs
            v-model="activeWkeID"
            show-arrows
            hide-slider
            :height="wkeNavHeight"
            class="tab-navigation--btn-style wke-navigation"
        >
            <v-tab
                v-for="wke in worksheets_arr"
                :key="wke.id"
                :href="`#${wke.id}`"
                class="tab-btn px-3 text-uppercase"
                active-class="tab-btn--active font-weight-medium"
            >
                {{ wke.name }}
                <v-btn
                    v-if="worksheets_arr.length > 1"
                    class="ml-2"
                    icon
                    x-small
                    @click="DELETE_WKE(worksheets_arr.indexOf(wke))"
                >
                    <v-icon size="8" color="error"> $vuetify.icons.close</v-icon>
                </v-btn>
            </v-tab>
            <v-btn height="32" width="32" class="ml-2" icon @click="addNewWs">
                <v-icon size="18" color="deep-ocean">add</v-icon>
            </v-btn>
        </v-tabs>

        <template v-for="wke in worksheets_arr">
            <keep-alive :key="wke.id">
                <worksheet
                    v-if="activeWkeID === wke.id"
                    ref="wke"
                    :style="{
                        height: `calc(100% - ${wkeNavHeight}px)`,
                    }"
                    :containerHeight="containerHeight"
                    :previewDataSchemaId="previewDataSchemaId"
                    :showVisSidebar="showVisSidebar"
                    v-on="$listeners"
                />
            </keep-alive>
        </template>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapMutations, mapState } from 'vuex'
import Worksheet from './Worksheet'
export default {
    name: 'worksheets',
    components: {
        Worksheet,
    },
    props: {
        containerHeight: { type: Number, required: true },
        previewDataSchemaId: { type: String, required: true },
        showVisSidebar: { type: Boolean, required: true },
    },
    data() {
        return {
            wkeNavHeight: 32,
        }
    },
    computed: {
        ...mapState({
            worksheets_arr: state => state.query.worksheets_arr,
            active_wke_id: state => state.query.active_wke_id,
        }),
        activeWkeID: {
            get() {
                return this.active_wke_id
            },
            set(v) {
                this.SET_ACTIVE_WKE_ID(v)
            },
        },
    },
    created() {
        this.ADD_NEW_WKE()
    },
    methods: {
        ...mapMutations({
            ADD_NEW_WKE: 'query/ADD_NEW_WKE',
            DELETE_WKE: 'query/DELETE_WKE',
            SET_ACTIVE_WKE_ID: 'query/SET_ACTIVE_WKE_ID',
        }),
        addNewWs() {
            this.ADD_NEW_WKE()
            this.SET_ACTIVE_WKE_ID(this.worksheets_arr[this.worksheets_arr.length - 1].id)
        },
    },
}
</script>
<style lang="scss" scoped>
.wke-navigation {
    border-right: 1px solid $table-border !important;
    .tab-btn {
        border-bottom: none !important;
        border-top: none !important;
    }
}
</style>
