<template>
    <v-sheet class="d-flex mb-2">
        <outlined-overview-card
            v-for="(value, name) in getTopOverviewInfo"
            :key="name"
            wrapperClass="mt-0"
            :cardClass="`card-${name} px-10`"
            :hoverableCard="name === 'master'"
            @card-hover="showEditBtn = $event"
        >
            <template v-slot:card-body>
                <span
                    class="text-caption text-uppercase font-weight-bold mxs-color-helper text-deep-ocean"
                >
                    {{ name.replace('_', ' ') }}
                </span>

                <template v-if="name === 'master'">
                    <router-link
                        v-if="value !== 'undefined'"
                        :to="`/dashboard/servers/${value}`"
                        class="text-no-wrap text-body-2 rsrc-link"
                    >
                        <span>{{ value }} </span>
                    </router-link>
                    <span v-else class="text-no-wrap text-body-2">
                        {{ value }}
                    </span>
                    <v-btn
                        v-show="showEditBtn"
                        class="switchover-edit-btn"
                        icon
                        @click="() => onEdit(switchoverOp.type)"
                    >
                        <v-icon size="18" color="primary">
                            $vuetify.icons.mxs_edit
                        </v-icon>
                    </v-btn>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        activator=".switchover-edit-btn"
                    >
                        {{ switchoverOp.text }}
                    </v-tooltip>
                </template>

                <span v-else class="text-no-wrap text-body-2">
                    {{ value }}
                </span>
            </template>
        </outlined-overview-card>

        <sel-dlg
            v-model="isSelectDlgOpened"
            :title="dialogTitle"
            saveText="swap"
            :entityName="targetSelectItemType"
            :itemsList="itemsList"
            :defaultItems="defaultItems"
            :onSave="confirmChange"
            @selected-items="targetItem = $event"
            @on-open="getAllEntities"
        >
            <template v-if="smallInfo" v-slot:body-append>
                <small class="d-inline-block mt-4">
                    {{ $mxs_t(smallInfo) }}
                </small>
            </template>
        </sel-dlg>
    </v-sheet>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'
import { MONITOR_OP_TYPES } from '@src/constants'

export default {
    name: 'overview-header',
    props: {
        currentMonitor: { type: Object, required: true },
    },
    data() {
        return {
            showEditBtn: false,
            dialogTitle: '',
            targetItem: [],
            //select dialog
            isSelectDlgOpened: false,
            targetSelectItemType: 'servers',
            itemsList: [],
            defaultItems: {},
            smallInfo: '',
        }
    },
    computed: {
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        getTopOverviewInfo() {
            /*
            Set fallback undefined value as string if properties doesnt exist
            This allows it to be render as text
          */
            const {
                attributes: {
                    monitor_diagnostics: { master, master_gtid_domain_id, state, primary } = {},
                } = {},
            } = this.currentMonitor

            const overviewInfo = {
                master,
                master_gtid_domain_id,
                state,
                primary,
            }
            Object.keys(overviewInfo).forEach(
                key =>
                    (overviewInfo[key] = this.$helpers.stringifyNullOrUndefined(overviewInfo[key]))
            )
            return overviewInfo
        },

        serverIds: function() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.currentMonitor
            return serversData.map(server => ({ id: server.id, type: server.type }))
        },
        switchoverOp() {
            return this.getMonitorOps({ scope: this })[MONITOR_OP_TYPES.SWITCHOVER]
        },
    },
    methods: {
        // get available entities and set default item when sel-dlg is opened
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'servers':
                    this.itemsList = await this.serverIds
                    this.defaultItems = {
                        id: this.getTopOverviewInfo.master,
                        type: 'servers',
                    }
                    break
            }
        },

        onEdit(type) {
            switch (type) {
                case MONITOR_OP_TYPES.SWITCHOVER:
                    this.dialogTitle = this.switchoverOp.text
                    this.smallInfo = 'info.switchover'
                    this.targetSelectItemType = 'servers'
                    break
            }
            this.isSelectDlgOpened = true
        },

        async confirmChange() {
            switch (this.targetSelectItemType) {
                case 'servers':
                    {
                        const { id: masterId } = this.targetItem[0]
                        this.$emit('switch-over', masterId)
                    }
                    break
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.switchover-edit-btn {
    position: absolute;
    right: 10px;
    bottom: 10px;
}
</style>
