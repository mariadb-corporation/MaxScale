<template>
    <!-- SERVICE TABLE -->
    <v-col cols="12" class="pa-0 mt-4">
        <collapse
            :toggleOnClick="() => (showServices = !showServices)"
            :isContentVisible="showServices"
            :title="`${$tc('services', 2)}`"
            :titleInfo="serviceTableRow.length"
            :onAddClick="() => onAdd('services')"
            :addBtnText="`${$t('addEntity', { entityName: $tc('services', 1) })}`"
        >
            <template v-slot:content>
                <data-table
                    :search="searchKeyWord"
                    :headers="servicesTableHeader"
                    :data="serviceTableRow"
                    :sortDesc="false"
                    :noDataText="$t('noEntity', { entityName: $tc('services', 2) })"
                    sortBy="id"
                    :loading="loading"
                    showActionsOnHover
                >
                    <template v-slot:id="{ data: { item: { id } } }">
                        <router-link
                            :key="id"
                            :to="`/dashboard/services/${id}`"
                            class="no-underline"
                        >
                            {{ id }}
                        </router-link>
                    </template>
                    <template v-slot:state="{ data: { item: { state } } }">
                        <icon-sprite-sheet
                            size="13"
                            class="status-icon"
                            :frame="$help.serviceStateIcon(state)"
                        >
                            status
                        </icon-sprite-sheet>
                    </template>
                    <template v-slot:actions="{ data: { item } }">
                        <v-btn icon @click="onDelete('services', item)">
                            <v-icon size="20" color="error">
                                $vuetify.icons.unlink
                            </v-icon>
                        </v-btn>
                    </template>
                </data-table>
            </template>
        </collapse>
        <!-- Avaiable dialog for Service Table -->
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
            @on-open="getAllEntities"
        />
    </v-col>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'services-table',

    props: {
        searchKeyWord: { type: String, required: true },
        serviceTableRow: { type: Array, required: true },
        dispatchRelationshipUpdate: { type: Function, required: true },
        loading: { type: Boolean, required: true },
        getServiceState: { type: Function, required: true },
    },
    data() {
        return {
            // services
            showServices: true,
            servicesTableHeader: [
                { text: 'Service', value: 'id' },
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
            targetSelectItemType: 'services',
            itemsList: [],
        }
    },

    methods: {
        //--------------------------------------------------------- COMMON ---------------------------------------------
        // -------------- Delete handle
        onDelete(type, item) {
            this.targetItem = item
            switch (type) {
                case 'services':
                    this.deleteDialogType = 'unlink'
                    this.dialogTitle = `${this.$t('unlink')} ${this.$tc('services', 1)}`
                    break
            }

            this.showDeleteDialog = true
        },

        async confirmDelete() {
            let self = this
            switch (self.targetItem.type) {
                case 'services':
                    {
                        let ori = self.serviceTableRow
                        let servicesRelationship = []
                        for (let i = 0; i < ori.length; ++i) {
                            if (ori[i].id !== self.targetItem.id) {
                                let cloneO = self.$help.lodash.cloneDeep(ori[i])
                                delete cloneO.state
                                servicesRelationship.push(cloneO)
                            }
                        }

                        await self.dispatchRelationshipUpdate('services', servicesRelationship)
                    }
                    break
            }
        },

        // -------------- Add handle
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'services':
                    {
                        const self = this
                        const allServices = await self.getServiceState()
                        let availableEntities = self.$help.lodash.xorWith(
                            allServices,
                            self.serviceTableRow,
                            (a, b) => a.id === b.id
                        )
                        self.itemsList = availableEntities
                    }
                    break
            }
        },

        onAdd(type) {
            let self = this
            self.dialogTitle = `${self.$t(`addEntity`, {
                entityName: self.$tc(type, 2),
            })}`

            switch (type) {
                case 'services':
                    self.targetSelectItemType = 'services'
                    break
            }

            this.showSelectDialog = true
        },

        async confirmAdd() {
            let self = this

            switch (self.targetSelectItemType) {
                case 'services':
                    {
                        let ori = self.serviceTableRow
                        let merge = [...ori, ...self.targetItem]
                        let servicesRelationship = []
                        for (let i = 0; i < merge.length; ++i) {
                            let cloneO = self.$help.lodash.cloneDeep(merge[i])
                            delete cloneO.state
                            servicesRelationship.push(cloneO)
                        }
                        await self.dispatchRelationshipUpdate('services', servicesRelationship)
                    }
                    break
            }
        },
    },
}
</script>
