<template>
    <collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="`${$tc(relationshipType, 2)}`"
        :titleInfo="tableRowsData.length"
        :onAddClick="readOnly ? null : () => onAdd(relationshipType)"
        :addBtnText="readOnly ? '' : `${$t('addEntity', { entityName: $tc(relationshipType, 2) })}`"
    >
        <template v-slot:content>
            <data-table
                :search="searchKeyWord"
                :headers="tableHeader"
                :data="tableRowsData"
                :noDataText="$t('noEntity', { entityName: $tc(relationshipType, 2) })"
                :sortBy="relationshipType === 'filters' ? '' : 'id'"
                :loading="loading"
                :showActionsOnHover="!readOnly"
                :draggable="relationshipType === 'filters'"
                :hasOrderNumber="relationshipType === 'filters'"
                @on-drag-end="filterDragReorder"
            >
                <template
                    v-if="relationshipType !== 'filters'"
                    v-slot:id="{ data: { item: { id } } }"
                >
                    <router-link
                        :key="id"
                        :to="`/dashboard/${relationshipType}/${id}`"
                        class="no-underline"
                    >
                        {{ id }}
                    </router-link>
                </template>
                <template v-slot:state="{ data: { item: { state } } }">
                    <icon-sprite-sheet size="13" class="status-icon" :frame="getStatusIcon(state)">
                        status
                    </icon-sprite-sheet>
                </template>
                <template v-if="!readOnly" v-slot:actions="{ data: { item } }">
                    <v-btn icon @click="onDelete(relationshipType, item)">
                        <v-icon size="20" color="error">
                            $vuetify.icons.unlink
                        </v-icon>
                    </v-btn>
                </template>
            </data-table>
            <!-- Avaiable dialogs for editable table -->
            <confirm-dialog
                v-if="!readOnly"
                v-model="showDeleteDialog"
                :title="dialogTitle"
                :type="deleteDialogType"
                :item="Array.isArray(targetItem) ? {} : targetItem"
                :onSave="() => confirmDelete()"
                :onClose="() => (showDeleteDialog = false)"
                :onCancel="() => (showDeleteDialog = false)"
            />
            <select-dialog
                v-if="!readOnly"
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
        </template>
    </collapse>
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
import { mapGetters } from 'vuex'

export default {
    name: 'relationship-table',
    props: {
        relationshipType: { type: String, required: true }, // servers, services, filters
        tableRows: { type: Array, required: true },
        loading: { type: Boolean, required: true },
        readOnly: { type: Boolean, default: false },
        /*
            below props are required only when readOnly is false.
        */
        getRelationshipData: { type: Function },
        dispatchRelationshipUpdate: { type: Function },
    },
    data() {
        return {
            showTable: true,
            tableHeader: [
                { text: this.$tc(this.relationshipType, 1), value: 'id' },
                { text: 'Status', value: 'state', align: 'center' },
            ],

            //---------------- common
            dialogTitle: '',
            targetItem: null,
            //delete dialog
            showDeleteDialog: false,
            deleteDialogType: 'delete',
            //select dialog
            showSelectDialog: false,
            targetSelectItemType: this.relationshipType,
            itemsList: [],
        }
    },
    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
        }),
        logger: function() {
            return this.$store.Vue.Logger('relationship-table')
        },
        tableRowsData: function() {
            return this.tableRows
        },
    },
    watch: {
        getRelationshipData: {
            handler(value) {
                if (!this.readOnly && !this.$help.isFunction(value))
                    this.logger.error("property 'getRelationshipData' is required.")
            },
            immediate: true,
        },
        dispatchRelationshipUpdate: {
            handler(value) {
                if (!this.readOnly && !this.$help.isFunction(value))
                    this.logger.error("property 'dispatchRelationshipUpdate' is required.")
            },
            immediate: true,
        },
    },
    mounted() {
        this.assignTableHeaders(this.relationshipType)
    },
    methods: {
        assignTableHeaders(relationshipType) {
            switch (relationshipType) {
                case 'filters':
                    this.tableHeader = [
                        {
                            text: '',
                            value: 'index',
                            width: '1px',
                            padding: '0px!important',
                            sortable: false,
                        },
                        { text: 'Filter', value: 'id', sortable: false },
                        { text: '', value: 'action', sortable: false },
                    ]
                    break
                case 'servers':
                case 'services':
                    this.tableHeader = [
                        ...this.tableHeader,
                        { text: '', value: 'action', sortable: false },
                    ]
            }
        },

        //--------------------------------------------------------- FILTERS ------------------------------------------
        async filterDragReorder({ oldIndex, newIndex }) {
            let self = this
            if (oldIndex !== newIndex) {
                const moved = this.tableRowsData.splice(oldIndex, 1)[0]
                this.tableRowsData.splice(newIndex, 0, moved)
                const isFilterDrag = true
                await this.dispatchRelationshipUpdate('filters', self.tableRowsData, isFilterDrag)
            }
        },
        //--------------------------------------------------------- COMMON ---------------------------------------------
        getStatusIcon(state) {
            switch (this.relationshipType) {
                case 'services':
                    return this.$help.serviceStateIcon(state)
                case 'servers':
                    return this.$help.serverStateIcon(state)
                case 'listeners':
                    return this.$help.listenerStateIcon(state)
            }
        },
        // -------------- Delete handle
        onDelete(type, item) {
            this.targetItem = item
            switch (type) {
                case 'filters':
                case 'servers':
                case 'services':
                    this.deleteDialogType = 'unlink'
                    this.dialogTitle = `${this.$t('unlink')} ${this.$tc(this.relationshipType, 1)}`
                    break
            }

            this.showDeleteDialog = true
        },

        async confirmDelete() {
            /* const self = this */
            switch (this.targetItem.type) {
                case 'servers':
                case 'services':
                case 'filters':
                    {
                        let ori = this.tableRowsData
                        let relationship = []
                        for (let i = 0; i < ori.length; ++i) {
                            if (ori[i].id !== this.targetItem.id) {
                                let cloneO = this.$help.lodash.cloneDeep(ori[i])
                                delete cloneO.state
                                relationship.push(cloneO)
                            }
                        }

                        await this.dispatchRelationshipUpdate(this.relationshipType, relationship)
                    }
                    break
            }
        },

        // -------------- Add handle
        async getAllEntities() {
            switch (this.targetSelectItemType) {
                case 'servers':
                case 'services':
                case 'filters':
                    {
                        const self = this
                        const all = await this.getRelationshipData()
                        let availableEntities = this.$help.lodash.xorWith(
                            all,
                            self.tableRowsData,
                            (a, b) => a.id === b.id
                        )
                        this.itemsList = availableEntities
                    }
                    break
            }
        },

        onAdd(type) {
            let self = this
            self.dialogTitle = `${self.$t(`addEntity`, {
                entityName: self.$tc(type, 2),
            })}`
            this.targetSelectItemType = this.relationshipType
            this.showSelectDialog = true
        },

        async confirmAdd() {
            let self = this

            switch (self.targetSelectItemType) {
                case 'filters':
                case 'servers':
                case 'services':
                    {
                        let ori = self.tableRowsData
                        let merge = [...ori, ...self.targetItem]

                        let relationship = []
                        for (let i = 0; i < merge.length; ++i) {
                            let cloneO = self.$help.lodash.cloneDeep(merge[i])
                            delete cloneO.state
                            delete cloneO.attributes
                            delete cloneO.index
                            delete cloneO.links
                            relationship.push(cloneO)
                        }
                        await self.dispatchRelationshipUpdate(this.relationshipType, relationship)
                    }
                    break
            }
        },
    },
}
</script>
