<template>
    <mxs-collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="`${$mxs_tc(relationshipType, 2)}`"
        :titleInfo="tableRowsData.length"
        :onAddClick="isAdmin && addable ? onAdd : null"
        :addBtnText="isAdmin && addable ? addBtnText : ''"
    >
        <data-table
            :search="search_keyword"
            :headers="tableHeader"
            :data="tableRowsData"
            :noDataText="$mxs_t('noEntity', { entityName: $mxs_tc(relationshipType, 2) })"
            sortBy=""
            :loading="isLoading"
            :showActionsOnHover="removable"
            :draggable="relationshipType === 'filters'"
            :hasOrderNumber="relationshipType === 'filters'"
            showAll
            @on-drag-end="filterDragReorder"
        >
            <template v-slot:id="{ data: { item: { id } } }">
                <router-link
                    :key="id"
                    v-mxs-highlighter="{ keyword: search_keyword, txt: id }"
                    :to="`/dashboard/${relationshipType}/${id}`"
                    class="rsrc-link"
                >
                    {{ id }}
                </router-link>
            </template>
            <template v-slot:state="{ data: { item: { state } } }">
                <icon-sprite-sheet size="16" class="state-icon" :frame="getStatusIcon(state)">
                    {{ relationshipType }}
                </icon-sprite-sheet>
            </template>
            <template v-if="isAdmin && removable" v-slot:actions="{ data: { item } }">
                <v-btn icon @click="onDelete(item)">
                    <v-icon size="20" color="error">
                        $vuetify.icons.mxs_unlink
                    </v-icon>
                </v-btn>
            </template>
        </data-table>
        <!-- Available dialogs for editable table -->
        <mxs-conf-dlg
            v-if="removable"
            v-model="isConfDlgOpened"
            :title="dialogTitle"
            :saveText="deleteDialogType"
            :type="deleteDialogType"
            :item="$typy(targetItems, '[0]').safeObjectOrEmpty"
            :onSave="confirmDelete"
        />
        <mxs-sel-dlg
            v-if="addable"
            v-model="isSelectDlgOpened"
            :title="dialogTitle"
            saveText="add"
            multiple
            :entityName="relationshipType"
            :itemsList="itemsList"
            :onSave="confirmAdd"
            @selected-items="targetItems = $event"
            @on-open="getAllEntities"
        />
    </mxs-collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
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
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import { mapState, mapGetters } from 'vuex'

export default {
    name: 'relationship-table',
    props: {
        relationshipType: { type: String, required: true }, // servers, services, filters
        tableRows: { type: Array, required: true },
        removable: { type: Boolean, default: false },
        addable: { type: Boolean, default: false },
        selectItems: { type: Array },
        //below props are required only when removable is true.
        getRelationshipData: { type: Function },
    },
    data() {
        return {
            showTable: true,
            tableHeader: [
                {
                    text: this.$mxs_tc(this.relationshipType, 1),
                    value: 'id',
                    autoTruncate: true,
                    sortable: false,
                },
                { text: 'Status', value: 'state', align: 'center', sortable: false },
            ],

            //---------------- common
            dialogTitle: '',
            targetItems: [],
            //delete dialog
            deleteDialogType: 'delete',
            //select dialog
            itemsList: [],
            isMounting: true,
            isConfDlgOpened: false,
            isSelectDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            overlay_type: state => state.mxsApp.overlay_type,
            search_keyword: 'search_keyword',
        }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        isLoading() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        tableRowsData() {
            // add index number for filters table only
            if (this.relationshipType === 'filters')
                this.tableRows.forEach((row, i) => (row.index = i))
            return this.tableRows
        },
        addBtnText() {
            let pluralizationNum = 2
            if (this.relationshipType === 'listeners') pluralizationNum = 1
            return `${this.$mxs_t('addEntity', {
                entityName: this.$mxs_tc(this.relationshipType, pluralizationNum),
            })}`
        },
    },
    watch: {
        getRelationshipData: {
            handler(value) {
                if (this.removable && !this.$typy(value).isFunction)
                    this.$logger.error("property 'getRelationshipData' is required.")
            },
            immediate: true,
        },
    },
    async mounted() {
        this.assignTableHeaders(this.relationshipType)
        await this.$helpers.delay(400).then(() => (this.isMounting = false))
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
                            padding: '0px 8px',
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

                const clonedTableRowsData = this.$helpers.lodash.cloneDeep(this.tableRowsData)
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
                    return this.$helpers.serviceStateIcon(state)
                case 'servers':
                    return this.$helpers.serverStateIcon(state)
                case 'listeners':
                    return this.$helpers.listenerStateIcon(state)
            }
        },
        // -------------- Delete handle
        onDelete(item) {
            this.targetItems = [item]
            this.deleteDialogType = 'unlink'
            this.dialogTitle = `${this.$mxs_t('unlink')} ${this.$mxs_tc(this.relationshipType, 1)}`
            this.isConfDlgOpened = true
        },

        async confirmDelete() {
            this.$emit('on-relationship-update', {
                type: this.relationshipType,
                data: this.tableRowsData.reduce((arr, row) => {
                    if (this.targetItems.some(item => item.id !== row.id))
                        arr.push({ id: row.id, type: row.type })
                    return arr
                }, []),
            })
        },

        // -------------- Add handle
        async getAllEntities() {
            if (this.selectItems) {
                await this.getRelationshipData({ type: this.relationshipType })
                this.itemsList = this.selectItems
            } else {
                const all = await this.getRelationshipData({ type: this.relationshipType })
                const availableEntities = this.$helpers.lodash.xorWith(
                    all,
                    this.tableRowsData,
                    (a, b) => a.id === b.id
                )

                this.itemsList = availableEntities
            }
        },

        onAdd() {
            this.dialogTitle = `${this.$mxs_t(`addEntity`, {
                entityName: this.$mxs_tc(this.relationshipType, 2),
            })}`

            if (this.relationshipType !== 'listeners') this.isSelectDlgOpened = true
            else this.$emit('open-listener-form-dialog')
        },

        /**
         * @param {Array} arr - array of object, each object must have id and type attributes
         * @returns {Array} - returns valid relationship array data
         */
        formatRelationshipData(arr) {
            return arr.map(item => ({ id: item.id, type: item.type }))
        },

        async confirmAdd() {
            this.$emit('on-relationship-update', {
                type: this.relationshipType,
                data: [
                    ...this.formatRelationshipData(this.tableRowsData),
                    ...this.formatRelationshipData(this.targetItems),
                ],
            })
        },
    },
}
</script>
