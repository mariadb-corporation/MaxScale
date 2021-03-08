<template>
    <collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="`${$tc(relationshipType, 2)}`"
        :titleInfo="tableRowsData.length"
        :onAddClick="readOnly && !addable ? null : () => onAdd()"
        :addBtnText="readOnly && !addable ? '' : addBtnText"
    >
        <template v-slot:content>
            <data-table
                :search="search_keyword"
                :headers="tableHeader"
                :data="tableRowsData"
                :noDataText="$t('noEntity', { entityName: $tc(relationshipType, 2) })"
                :sortBy="relationshipType === 'filters' ? '' : 'id'"
                :loading="isLoading"
                :showActionsOnHover="!readOnly"
                :draggable="relationshipType === 'filters'"
                :hasOrderNumber="relationshipType === 'filters'"
                @on-drag-end="filterDragReorder"
            >
                <template v-slot:id="{ data: { item: { id } } }">
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
                    <v-btn icon @click="onDelete(item)">
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
                :entityName="relationshipType"
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
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component:
Emits:
- $emit('on-relationship-update', {
            type: this.relationshipType,
            data: this.tableRowsData,
            isFilterDrag: isFilterDrag,
        })
isFilterDrag will be only added to event data object if relationshipType props === 'filters'
- $emit('open-listener-form-dialog')
This callback event is emitted only when relationshipType props === 'listeners'
*/
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState } from 'vuex'

export default {
    name: 'relationship-table',
    props: {
        relationshipType: { type: String, required: true }, // servers, services, filters
        tableRows: { type: Array, required: true },
        readOnly: { type: Boolean, default: false },
        addable: { type: Boolean, default: true },
        selectItems: { type: Array },
        //below props are required only when readOnly is false.
        getRelationshipData: { type: Function },
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
            itemsList: [],
            isMounting: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            search_keyword: 'search_keyword',
        }),
        isLoading: function() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        logger: function() {
            return this.$logger('relationship-table')
        },
        tableRowsData: function() {
            // add index number for filters table only
            if (this.relationshipType === 'filters')
                this.tableRows.forEach((row, i) => (row.index = i))
            return this.tableRows
        },
        addBtnText: function() {
            let pluralizationNum = 2
            if (this.relationshipType === 'listeners') pluralizationNum = 1
            return `${this.$t('addEntity', {
                entityName: this.$tc(this.relationshipType, pluralizationNum),
            })}`
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
    },
    async mounted() {
        this.assignTableHeaders(this.relationshipType)
        await this.$help.delay(400).then(() => (this.isMounting = false))
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
            if (oldIndex !== newIndex) {
                const moved = this.tableRowsData.splice(oldIndex, 1)[0]
                this.tableRowsData.splice(newIndex, 0, moved)
                const isFilterDrag = true

                const clonedTableRowsData = this.$help.lodash.cloneDeep(this.tableRowsData)
                clonedTableRowsData.forEach(item => {
                    delete item.index
                })
                await this.$emit('on-relationship-update', {
                    type: this.relationshipType,
                    data: clonedTableRowsData,
                    isFilterDrag: isFilterDrag,
                })
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
        onDelete(item) {
            this.targetItem = item
            this.deleteDialogType = 'unlink'
            this.dialogTitle = `${this.$t('unlink')} ${this.$tc(this.relationshipType, 1)}`
            this.showDeleteDialog = true
        },

        async confirmDelete() {
            const rows = this.$help.lodash.cloneDeep(this.tableRowsData)
            let relationship = []
            rows.forEach(item => {
                if (item.id !== this.targetItem.id) {
                    delete item.state
                    delete item.attributes
                    delete item.index
                    delete item.links
                    relationship.push(item)
                }
            })
            await this.$emit('on-relationship-update', {
                type: this.relationshipType,
                data: relationship,
            })
        },

        // -------------- Add handle
        async getAllEntities() {
            if (this.selectItems) {
                await this.getRelationshipData(this.relationshipType)
                this.itemsList = this.selectItems
            } else {
                const all = await this.getRelationshipData(this.relationshipType)
                const availableEntities = this.$help.lodash.xorWith(
                    all,
                    this.tableRowsData,
                    (a, b) => a.id === b.id
                )

                this.itemsList = availableEntities
            }
        },

        onAdd() {
            this.dialogTitle = `${this.$t(`addEntity`, {
                entityName: this.$tc(this.relationshipType, 2),
            })}`

            if (this.relationshipType !== 'listeners') this.showSelectDialog = true
            else this.$emit('open-listener-form-dialog')
        },

        async confirmAdd() {
            const rows = this.$help.lodash.cloneDeep(this.tableRowsData)
            let relationship = [...rows, ...this.targetItem]
            relationship.forEach(item => {
                delete item.state
                delete item.attributes
                delete item.index
                delete item.links
            })
            await this.$emit('on-relationship-update', {
                type: this.relationshipType,
                data: relationship,
            })
        },
    },
}
</script>
