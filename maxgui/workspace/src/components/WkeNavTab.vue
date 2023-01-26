<template>
    <v-hover v-slot="{ hover: isHovered }">
        <div
            :style="{ width: '160px' }"
            class="fill-height d-flex align-center justify-space-between px-3"
        >
            <v-tooltip
                :disabled="!$typy(wkeConn, 'name').safeString"
                top
                transition="slide-x-transition"
            >
                <template v-slot:activator="{ on }">
                    <div class="d-inline-flex align-center" v-on="on">
                        <span
                            class="tab-name d-inline-block text-truncate"
                            :style="{ maxWidth: '110px' }"
                        >
                            {{ wke.name }}
                        </span>
                        <v-progress-circular
                            v-if="isWkeLoadingQueryResult"
                            class="ml-2"
                            size="16"
                            width="2"
                            color="primary"
                            indeterminate
                        />
                    </div>
                </template>
                <template v-if="!$typy(wke, 'active_query_tab_id').isNull">
                    {{ $mxs_t('connectedTo') }}
                </template>
                {{ $typy(wkeConn, 'name').safeString }}
            </v-tooltip>
            <v-menu
                :disabled="!isEtlWorksheet"
                transition="slide-y-transition"
                offset-y
                left
                content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
            >
                <template v-slot:activator="{ on, attrs }">
                    <v-btn
                        v-show="isHovered"
                        class="ml-1"
                        icon
                        x-small
                        v-bind="attrs"
                        @click.stop.prevent="
                            isEtlWorksheet
                                ? null
                                : $emit('on-choose-action', { type: WKE_ACTION_TYPES.DELETE, wke })
                        "
                        v-on="on"
                    >
                        <v-icon
                            :size="isEtlWorksheet ? 12 : 8"
                            :color="isEtlWorksheet ? 'accent-dark' : isConnBusy ? '' : 'error'"
                        >
                            {{ `$vuetify.icons.${isEtlWorksheet ? 'mxs_edit' : 'mxs_close'}` }}
                        </v-icon>
                    </v-btn>
                </template>
                <v-list>
                    <v-list-item
                        v-for="action in actions"
                        :key="action.text"
                        :disabled="action.disabled"
                        @click="$emit('on-choose-action', { type: action.type, wke })"
                    >
                        <v-list-item-title
                            class="mxs-color-helper"
                            :class="[
                                action.type === WKE_ACTION_TYPES.DELETE
                                    ? 'text-error'
                                    : 'text-text',
                            ]"
                        >
                            {{ action.text }}
                        </v-list-item-title>
                    </v-list-item>
                </v-list>
            </v-menu>
        </div>
    </v-hover>
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
/**
 * Emit:
 * @on-choose-action: { type: WKE_ACTION_TYPES, wke: object}
 */
import Worksheet from '@wsModels/Worksheet'
import QueryTab from '@wsModels/QueryTab'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import { mapState } from 'vuex'

export default {
    name: 'wke-nav-tab',
    props: {
        wke: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            WKE_ACTION_TYPES: state => state.mxsWorkspace.config.WKE_ACTION_TYPES,
        }),
        totalWorksheets() {
            return Worksheet.all().length
        },
        wkeId() {
            return this.wke.id
        },
        wkeConn() {
            return QueryConn.getters('getWkeConnByWkeId')(this.wkeId)
        },
        isWkeLoadingQueryResult() {
            const queryTabs = QueryTab.getters('getQueryTabsByWkeId')(this.wkeId)
            let isLoading = false
            for (const { id } of queryTabs) {
                if (QueryResult.getters('getLoadingQueryResultByQueryTabId')(id)) {
                    isLoading = true
                    break
                }
            }
            return isLoading
        },
        isConnBusy() {
            return QueryConn.getters('getIsConnBusyByQueryTabId')(
                Worksheet.getters('getActiveQueryTabId')
            )
        },
        isEtlWorksheet() {
            return !this.$typy(this.wke, 'active_etl_task_id').isNull
        },
        actions() {
            const { DELETE } = this.WKE_ACTION_TYPES
            return Object.values(this.WKE_ACTION_TYPES).map(type => ({
                text: this.$mxs_t(`wkeActions.${type}`),
                type,
                disabled: type === DELETE ? this.isConnBusy : false,
            }))
        },
    },
}
</script>
