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
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active font-weight-medium"
                :ripple="activeWkeID !== wke.id"
            >
                <div
                    style="min-width:160px"
                    class="fill-height d-flex align-center justify-space-between px-3"
                    @click.prevent="activeWkeID === wke.id ? (editableTabName = true) : () => null"
                >
                    <v-text-field
                        v-if="activeWkeID === wke.id && editableTabName"
                        v-model="wke.name"
                        autofocus
                        height="32"
                        class="std tab-name-input ma-0 pa-0"
                        hide-details
                        @blur="editableTabName = false"
                    />
                    <truncate-string v-else :text="wke.name" :maxWidth="112" />

                    <v-btn
                        v-if="worksheets_arr.length > 1"
                        class="ml-1 del-wke-btn"
                        icon
                        x-small
                        @click="DELETE_WKE(worksheets_arr.indexOf(wke))"
                    >
                        <v-icon size="8" color="error"> $vuetify.icons.close</v-icon>
                    </v-btn>
                </div>
            </v-tab>
            <v-btn height="32" width="32" class="ml-2" icon @click="addNewWs">
                <v-icon size="18" color="deep-ocean">add</v-icon>
            </v-btn>
        </v-tabs>
        <toolbar-container ref="toolbarContainer" />
        <v-fade-transition mode="out-in">
            <keep-alive>
                <worksheet
                    v-if="activeWkeID"
                    :key="activeWkeID"
                    :ctrDim="ctrDim"
                    :style="{
                        height: `calc(100% - ${wkeNavHeight + 45}px)`,
                    }"
                    @onCtrlEnter="() => $refs.toolbarContainer.handleRun('all')"
                    @onCtrlShiftEnter="() => $refs.toolbarContainer.handleRun('selected')"
                />
            </keep-alive>
        </v-fade-transition>
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
import ToolbarContainer from './ToolbarContainer'
export default {
    name: 'worksheets',
    components: {
        Worksheet,
        ToolbarContainer,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            wkeNavHeight: 32,
            editableTabName: false,
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
                if (v) this.SET_ACTIVE_WKE_ID(v)
            },
        },
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
    border-top: 1px solid $table-border !important;
    .tab-btn {
        border-bottom: none !important;
        border-top: none !important;
        .del-wke-btn {
            visibility: hidden;
        }
        &:hover {
            .del-wke-btn {
                visibility: visible;
            }
        }
    }
}
.tab-btn--active {
    cursor: text !important;
}
::v-deep .tab-name-input {
    height: 100%;
    font-size: 0.75rem;
    max-width: 112px;
    .v-input__slot {
        input {
            letter-spacing: 1.07143px; // same letter-spacing of v-tab
            padding: 0px;
            padding-top: 1px;
            line-height: 30px;
        }
        &::after,
        &::before {
            display: none;
        }
    }
}
</style>
