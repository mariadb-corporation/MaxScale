<template>
    <v-col cols="6">
        <collapse
            :toggleOnClick="() => (showServers = !showServers)"
            :isContentVisible="showServers"
            :title="`${$tc('servers', 2)}`"
            :titleInfo="serverStateTableRow.length"
            :onAddClick="() => onAdd('servers')"
            :addBtnText="`${$t('addEntity', { entityName: $tc('servers', 1) })}`"
        >
            <template v-slot:content>
                <data-table
                    :headers="serversTableHeader"
                    :data="serverStateTableRow"
                    :noDataText="$t('noEntity', { entityName: $tc('servers', 2) })"
                    sortBy="id"
                    :loading="loading"
                    :search="searchKeyWord"
                    showActionsOnHover
                >
                    <template v-slot:id="{ data: { item: { id } } }">
                        <router-link
                            :key="id"
                            :to="`/dashboard/servers/${id}`"
                            class="no-underline"
                        >
                            <span> {{ id }} </span>
                        </router-link>
                    </template>
                    <template v-slot:state="{ data: { item: { state } } }">
                        <icon-sprite-sheet
                            size="13"
                            class="status-icon"
                            :frame="$help.serverStateIcon(state)"
                        >
                            status
                        </icon-sprite-sheet>
                    </template>
                    <template v-slot:actions="{ data: { item } }">
                        <v-btn icon @click="onDelete('servers', item)">
                            <v-icon size="20" color="error">
                                $vuetify.icons.unlink
                            </v-icon>
                        </v-btn>
                    </template>
                </data-table>
                <confirm-dialog
                    v-model="showDeleteDialog"
                    :title="dialogTitle"
                    :type="deleteDialogType"
                    :item="Array.isArray(targetItem) ? {} : targetItem"
                    :onSave="() => confirmDelete()"
                    :onClose="() => (showDeleteDialog = false)"
                    :onCancel="() => (showDeleteDialog = false)"
                />

                <select-dialog
                    v-model="showSelectDialog"
                    :title="dialogTitle"
                    mode="add"
                    multiple
                    :entityName="targetSelectItemType"
                    :onClose="() => (showSelectDialog = false)"
                    :onCancel="() => (showSelectDialog = false)"
                    :handleSave="confirmAdd"
                    :itemsList="itemsList"
                    @selected-items="targetItem = $event"
                    @onOpen="getAllEntities"
                />
            </template>
        </collapse>
    </v-col>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'servers-table',
    props: {
        searchKeyWord: { type: String, required: true },
        currentMonitor: { type: Object, required: true },
        getServers: { type: Function, required: true },
        loading: { type: Boolean, required: true },
        dispatchRelationshipUpdate: { type: Function, required: true },
        serverStateTableRow: { type: Array, required: true },
    },
    data() {
        return {
            // servers
            showServers: true,
            serversTableHeader: [
                { text: 'Server', value: 'id' },
                { text: 'Status', value: 'state', align: 'center' },
                { text: '', value: 'action', sortable: false },
            ],
            //---------------- common
            dialogTitle: '',
            targetItem: null,
            //delete dialog
            showDeleteDialog: false,
            deleteDialogType: 'delete',

            //select dialog
            showSelectDialog: false,
            targetSelectItemType: 'servers',
            itemsList: [],
        }
    },

    methods: {
        //--------------------------------------------------------- COMMON ---------------------------------------------

        // -------------- Delete handle
        onDelete(type, item) {
            this.targetItem = item
            this.deleteDialogType = 'unlink'
            this.dialogTitle = `${this.$t('unlink')} ${this.$tc('servers', 1)}`

            this.showDeleteDialog = true
        },

        async confirmDelete() {
            let self = this
            let ori = self.serverStateTableRow
            let serversRelationship = []
            for (let i = 0; i < ori.length; ++i) {
                if (ori[i].id !== self.targetItem.id) {
                    let cloneO = self.$help.lodash.cloneDeep(ori[i])
                    delete cloneO.state
                    serversRelationship.push(cloneO)
                }
            }

            await self.dispatchRelationshipUpdate(serversRelationship)
        },
        // -------------- Add handle
        async getAllEntities() {
            let self = this
            let data = await this.getServers()

            // only allow to add unmonitored servers
            let availableEntities = []
            for (let i = 0; i < data.length; ++i) {
                let server = data[i]
                // only allow to add unmonitored servers
                self.$help.lodash.isEmpty(server.relationships) &&
                    availableEntities.push({
                        id: server.id,
                        type: server.type,
                        state: server.attributes.state,
                    })
            }
            self.itemsList = availableEntities
        },

        onAdd(type) {
            let self = this
            self.dialogTitle = `${self.$t(`addEntity`, {
                entityName: self.$tc(type, 2),
            })}`

            self.targetSelectItemType = 'servers'
            this.showSelectDialog = true
        },

        async confirmAdd() {
            let self = this
            let ori = self.serverStateTableRow
            let merge = [...ori, ...self.targetItem]
            let serversRelationship = []
            for (let i = 0; i < merge.length; ++i) {
                let cloneO = self.$help.lodash.cloneDeep(merge[i])
                delete cloneO.state
                serversRelationship.push(cloneO)
            }
            await self.dispatchRelationshipUpdate(serversRelationship)
        },
    },
}
</script>
