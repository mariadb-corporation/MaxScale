<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeWkeID"
            show-arrows
            hide-slider
            :height="height"
            class="v-tabs--query-editor-style query-tab-nav v-tabs--custom-small-pagination-btn flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${pageToolbarBtnWidth + 1}px)` }"
            center-active
        >
            <v-tab
                v-for="wke in worksheets_arr"
                :key="wke.id"
                :href="`#${wke.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <v-tooltip
                    :disabled="!$typy(getWkeConnByWkeId(wke.id), 'name').safeString"
                    top
                    transition="slide-x-transition"
                    content-class="shadow-drop white"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            style="min-width:160px"
                            class="fill-height d-flex align-center justify-space-between px-3"
                            v-on="on"
                        >
                            <div class="d-inline-flex align-center">
                                <span
                                    class="tab-name d-inline-block text-truncate"
                                    style="max-width:88px"
                                >
                                    {{ wke.name }}
                                </span>
                                <v-progress-circular
                                    v-if="isWkeLoadingQueryResult(wke.id)"
                                    class="ml-2"
                                    size="16"
                                    width="2"
                                    color="primary"
                                    indeterminate
                                />
                            </div>
                            <v-btn
                                v-if="worksheets_arr.length > 1"
                                class="ml-1 del-tab-btn"
                                icon
                                x-small
                                :disabled="
                                    $typy(is_conn_busy_map[getActiveSessionId], 'value').safeBoolean
                                "
                                @click.stop.prevent="handleDeleteWke(wke.id)"
                            >
                                <v-icon
                                    size="8"
                                    :color="
                                        $typy(is_conn_busy_map[getActiveSessionId], 'value')
                                            .safeBoolean
                                            ? ''
                                            : 'error'
                                    "
                                >
                                    $vuetify.icons.mxs_close
                                </v-icon>
                            </v-btn>
                        </div>
                    </template>
                    <span class="mxs-color-helper text-text py-2 px-4">
                        {{ $mxs_t('connectedTo') }}
                        {{ $typy(getWkeConnByWkeId(wke.id), 'name').safeString }}
                    </span>
                </v-tooltip>
            </v-tab>
        </v-tabs>
        <wke-toolbar @get-total-btn-width="pageToolbarBtnWidth = $event" />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import WkeToolbar from './WkeToolbar.vue'

export default {
    name: 'wke-nav-ctr',
    components: { WkeToolbar },
    props: {
        height: { type: Number, required: true },
    },
    data() {
        return {
            pageToolbarBtnWidth: 128,
        }
    },
    computed: {
        ...mapState({
            worksheets_arr: state => state.wke.worksheets_arr,
            active_wke_id: state => state.wke.active_wke_id,
            is_conn_busy_map: state => state.queryConn.is_conn_busy_map,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getWkeConnByWkeId: 'queryConn/getWkeConnByWkeId',
            isWkeLoadingQueryResult: 'queryResult/isWkeLoadingQueryResult',
        }),
        activeWkeID: {
            get() {
                return this.active_wke_id
            },
            set(v) {
                if (v) this.SET_ACTIVE_WKE_ID(v)
            },
        },
    },
    methods: {
        ...mapMutations({ SET_ACTIVE_WKE_ID: 'wke/SET_ACTIVE_WKE_ID' }),
        ...mapActions({ handleDeleteWke: 'wke/handleDeleteWke' }),
    },
}
</script>
