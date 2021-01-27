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
                <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                    {{ name.replace('_', ' ') }}
                </span>

                <template v-if="name === 'master'">
                    <router-link
                        v-if="value !== 'undefined'"
                        :to="`/dashboard/servers/${value}`"
                        class="text-no-wrap body-2 no-underline"
                    >
                        <span>{{ value }} </span>
                    </router-link>
                    <span v-else class="text-no-wrap body-2">
                        {{ value }}
                    </span>
                    <v-btn
                        v-show="showEditBtn"
                        class="switchover-edit-btn"
                        icon
                        @click="() => onEdit('switchover')"
                    >
                        <v-icon size="18" color="primary">
                            $vuetify.icons.edit
                        </v-icon>
                    </v-btn>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                        activator=".switchover-edit-btn"
                    >
                        <span>{{ $t('switchover') }} </span>
                    </v-tooltip>
                </template>

                <span v-else class="text-no-wrap body-2">
                    {{ value }}
                </span>
            </template>
        </outlined-overview-card>

        <select-dialog
            v-model="showSelectDialog"
            :title="dialogTitle"
            mode="swap"
            :entityName="targetSelectItemType"
            :onClose="handleClose"
            :onCancel="handleClose"
            :handleSave="confirmChange"
            :itemsList="itemsList"
            :defaultItems="defaultItems"
            @selected-items="targetItem = $event"
            @on-open="getAllEntities"
        >
            <template v-if="smallInfo" v-slot:body-append>
                <small class="d-inline-block mt-4">
                    {{ $t(smallInfo) }}
                </small>
            </template>
        </select-dialog>
    </v-sheet>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
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
            showSelectDialog: false,
            targetSelectItemType: 'servers',
            itemsList: [],
            defaultItems: {},
            smallInfo: '',
        }
    },
    computed: {
        getTopOverviewInfo: function() {
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
                key => (overviewInfo[key] = this.$help.convertType(overviewInfo[key]))
            )
            return overviewInfo
        },

        serverIds: function() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.currentMonitor
            return serversData.map(server => ({ id: server.id, type: server.type }))
        },
    },
    methods: {
        handleClose() {
            this.showSelectDialog = false
        },
        // get available entities and set default item when select-dialog is opened
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
            this.dialogTitle = `${this.$t(`changeEntity`, {
                entityName: this.$tc(type, 1),
            })}`
            switch (type) {
                case 'switchover':
                    this.dialogTitle = `${this.$t(type)}`
                    this.smallInfo = 'info.switchover'
                    this.targetSelectItemType = 'servers'
                    break
            }
            this.showSelectDialog = true
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
