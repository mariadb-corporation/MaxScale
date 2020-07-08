<template>
    <v-row>
        <v-col class="py-0 my-0" cols="4">
            <v-row class="pa-0 ma-0">
                <!-- SERVER TABLE -->
                <v-col cols="12" class="pa-0 ma-0">
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
                                :search="searchKeyWord"
                                :headers="serversTableHeader"
                                :data="serverStateTableRow"
                                :sortDesc="false"
                                :noDataText="$t('noEntity', { entityName: $tc('servers', 2) })"
                                sortBy="id"
                                :loading="loading"
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
                        </template>
                    </collapse>
                </v-col>
                <!-- Filter TABLE -->
                <v-col cols="12" class="pa-0 mt-4">
                    <collapse
                        :toggleOnClick="() => (showFilter = !showFilter)"
                        :isContentVisible="showFilter"
                        :title="`${$tc('filters', 2)}`"
                        :titleInfo="filtersLinked.length"
                        :onAddClick="() => onAdd('filters')"
                        :addBtnText="`${$t('addEntity', { entityName: $tc('filters', 1) })}`"
                    >
                        <template v-slot:content>
                            <data-table
                                :headers="filterTableHeader"
                                :data="filtersLinked"
                                :sortDesc="false"
                                :noDataText="$t('noEntity', { entityName: $tc('filters', 2) })"
                                draggable
                                :loading="loading"
                                showActionsOnHover
                                :search="searchKeyWord"
                                hasOrderNumber
                                @on-drag-end="filterDragReorder"
                            >
                                <!-- <template v-slot:id="{ data: { item: { id } } }">
                                    <router-link
                                        :key="id"
                                        :to="`/dashboard/filters/${id}`"
                                        class="no-underline"
                                    >
                                        <span> {{ id }} </span>
                                    </router-link>
                                </template> -->
                                <template v-slot:actions="{ data: { item } }">
                                    <v-btn icon @click="onDelete('filters', item)">
                                        <v-icon size="14" color="error">
                                            $vuetify.icons.delete
                                        </v-icon>
                                    </v-btn>
                                </template>
                            </data-table>
                        </template>
                    </collapse>
                </v-col>
                <!-- Avaiable dialog for both SERVERS/FILTERS Tables -->
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
            </v-row>
        </v-col>
        <sessions-table
            :loading="loading"
            :sessionsByService="sessionsByService"
            :searchKeyWord="searchKeyWord"
        />
    </v-row>
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

import { mapMutations, mapGetters, mapActions } from 'vuex'
import SessionsTable from './SessionsTable'

export default {
    name: 'server-session-tab',
    components: {
        SessionsTable,
    },
    props: {
        searchKeyWord: { type: String, required: true },
        currentService: { type: Object, required: true },
        getServerState: { type: Function, required: true },
        loading: { type: Boolean, required: true },
        dispatchRelationshipUpdate: { type: Function, required: true },
        sessionsByService: { type: Array, required: true },
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
            // filters
            showFilter: true,
            filterTableHeader: [
                {
                    text: '',
                    value: 'index',
                    width: '1px',
                    padding: '0px!important',
                    sortable: false,
                },
                { text: 'Filter', value: 'id', sortable: false },
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

    computed: {
        ...mapGetters({
            allFilters: 'filter/allFilters',
        }),
        filtersLinked: function() {
            const {
                filters: { data: filtersLinkedData = [] } = {},
            } = this.currentService.relationships
            return filtersLinkedData
                ? filtersLinkedData.map((item, i) => ({ id: item.id, type: item.type, index: i }))
                : []
        },
    },

    methods: {
        ...mapMutations({
            showOverlay: 'showOverlay',
            hideOverlay: 'hideOverlay',
        }),
        ...mapActions({
            fetchAllFilters: 'filter/fetchAllFilters',
        }),
        //--------------------------------------------------------- FILTERS ------------------------------------------
        async filterDragReorder({ oldIndex, newIndex }) {
            let self = this
            if (oldIndex !== newIndex) {
                const moved = self.filtersLinked.splice(oldIndex, 1)[0]
                self.filtersLinked.splice(newIndex, 0, moved)
                await self.dispatchRelationshipUpdate('filters', self.filtersLinked)
            }
        },

        //--------------------------------------------------------- COMMON ---------------------------------------------

        // -------------- Delete handle
        onDelete(type, item) {
            this.targetItem = item
            switch (type) {
                case 'filters':
                    this.deleteDialogType = 'delete'
                    this.dialogTitle = `${this.$t('delete')} ${this.$tc('filters', 1)}`
                    break
                case 'servers':
                    this.deleteDialogType = 'unlink'
                    this.dialogTitle = `${this.$t('unlink')} ${this.$tc('servers', 1)}`
                    break
            }

            this.showDeleteDialog = true
        },

        async confirmDelete() {
            let self = this
            switch (self.targetItem.type) {
                case 'filters':
                    await self.dispatchRelationshipUpdate(
                        'filters',
                        self.filtersLinked.filter(item => item.id !== self.targetItem.id)
                    )
                    break
                case 'servers':
                    {
                        let ori = self.serverStateTableRow
                        let serversRelationship = []
                        for (let i = 0; i < ori.length; ++i) {
                            if (ori[i].id !== self.targetItem.id) {
                                let cloneO = self.$help.lodash.cloneDeep(ori[i])
                                delete cloneO.state
                                serversRelationship.push(cloneO)
                            }
                        }

                        await self.dispatchRelationshipUpdate('servers', serversRelationship)
                    }
                    break
            }
        },
        // -------------- Add handle
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'servers':
                    {
                        const self = this
                        const allServers = await self.getServerState()
                        let availableEntities = self.$help.lodash.xorWith(
                            allServers,
                            self.serverStateTableRow,
                            (a, b) => a.id === b.id
                        )
                        self.itemsList = availableEntities
                    }
                    break
                case 'filters':
                    {
                        const self = this
                        await self.fetchAllFilters()
                        let availableEntities = self.$help.lodash.xorWith(
                            self.allFilters,
                            self.filtersLinked,
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
                case 'filters':
                    self.targetSelectItemType = 'filters'
                    break
                case 'servers':
                    self.targetSelectItemType = 'servers'
                    break
            }

            this.showSelectDialog = true
        },
        async confirmAdd() {
            let self = this

            switch (self.targetSelectItemType) {
                case 'filters':
                    {
                        let clone = self.$help.lodash.cloneDeep(self.filtersLinked)
                        await self.dispatchRelationshipUpdate('filters', [
                            ...clone,
                            ...self.targetItem,
                        ])
                    }

                    break
                case 'servers':
                    {
                        let ori = self.serverStateTableRow
                        let merge = [...ori, ...self.targetItem]
                        let serversRelationship = []
                        for (let i = 0; i < merge.length; ++i) {
                            let cloneO = self.$help.lodash.cloneDeep(merge[i])
                            delete cloneO.state
                            serversRelationship.push(cloneO)
                        }
                        await self.dispatchRelationshipUpdate('servers', serversRelationship)
                    }
                    break
            }
        },
    },
}
</script>
