<template>
    <v-menu
        transition="slide-y-transition"
        :close-on-content-click="false"
        open-on-hover
        offset-y
        nudge-left="20"
        content-class="v-menu--with-arrow v-menu--with-arrow--top-left shadow-drop"
    >
        <template v-slot:activator="{ on }">
            <div class="pointer" :class="className" v-on="on">
                <status-icon size="20" type="config_sync" :value="data.status" />
                <span class="grayed-out-info">{{ configSyncStatusLabel }}</span>
            </div>
        </template>
        <v-sheet style="border-radius: 10px;" class="px-6 py-6">
            <p class="text-body-2 font-weight-bold mxs-color-helper text-navigation text-uppercase">
                {{ $mxs_t('configSync') }}
            </p>
            <mxs-treeview
                class="mxs-treeview--config-sync overflow-y-auto rounded"
                :items="treeData"
                dense
                item-text="id"
                open-all
            >
                <template v-slot:label="{ item: node }">
                    <div class="d-flex align-center justify-space-between">
                        <span class="d-inline-block text-truncate mr-10">{{ node.id }}</span>
                        <span class="d-inline-block text-truncate">{{ node.value }}</span>
                    </div>
                </template>
            </mxs-treeview>
        </v-sheet>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { objToTree } from '@src/utils/dataTableHelpers'

export default {
    name: 'config-sync',
    props: { data: { type: Object, required: true }, className: { type: [Object, Array, String] } },
    computed: {
        configSyncStatusLabel() {
            switch (this.data.status) {
                case 'No configuration changes':
                    return this.data.status
                case 'OK':
                    return this.$mxs_t('configSynced')
                default:
                    return this.$mxs_t('configSyncFailed')
            }
        },
        treeData() {
            return objToTree({ obj: this.data, keepPrimitiveValue: true, level: 0 })
        },
    },
}
</script>

<style lang="scss">
.mxs-treeview--config-sync {
    max-height: 50vh;
    font-size: 0.875rem;
    border-top: thin solid $table-border;
    .v-treeview-node__root {
        border: thin solid $table-border;
        border-top: none;
        padding-right: 36px;
        &:hover {
            background-color: $tr-hovered-color;
        }
    }
}
</style>
