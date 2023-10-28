<template>
    <collapse
        :toggleOnClick="() => (showTable = !showTable)"
        :isContentVisible="showTable"
        :title="$t('routingTargets')"
        :titleInfo="tableRows.length"
        :onAddClick="onEdit"
        :addBtnText="$t('edit')"
    >
        <data-table
            :search="search_keyword"
            :headers="tableHeader"
            :data="tableRows"
            :noDataText="$t('noEntity', { entityName: $t('routingTargets') })"
            sortBy=""
            :loading="isLoading"
            showActionsOnHover
            showAll
        >
            <template v-slot:id="{ data: { item: { id, state, type } } }">
                <icon-sprite-sheet
                    size="13"
                    class="mr-1 status-icon"
                    :frame="getStatusIcon({ state, type })"
                >
                    status
                </icon-sprite-sheet>
                <router-link :key="id" :to="`/dashboard/${type}/${id}`" class="rsrc-link">
                    {{ id }}
                </router-link>
            </template>
            <template v-slot:actions="{ data: { item } }">
                <v-btn icon @click="onDelete(item)">
                    <v-icon size="20" color="error">
                        $vuetify.icons.unlink
                    </v-icon>
                </v-btn>
            </template>
        </data-table>
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="dialogTitle"
            type="unlink"
            :item="$typy(targetItems, '[0]').safeObjectOrEmpty"
            :onSave="confirmDelete"
        />
        <routing-target-dlg
            v-model="isRoutingTargetDlgOpened"
            :title="dialogTitle"
            :routerId="routerId"
            :onSave="confirmEdit"
            :initialRoutingTargetHash="initialRoutingTargetHash"
            @selected-items="targetItems = $event"
        />
    </collapse>
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

import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapState } from 'vuex'
import RoutingTargetDlg from './RoutingTargetDlg.vue'

export default {
    name: 'routing-target-table',
    components: { RoutingTargetDlg },
    props: {
        tableRows: { type: Array, required: true },
        routerId: { type: String, required: true }, // the id of the MaxScale object being altered
    },
    data() {
        return {
            isMounting: true,
            showTable: true,
            //---------------- common
            dialogTitle: '',
            targetItems: [],
            isConfDlgOpened: false,
            isRoutingTargetDlgOpened: false,
        }
    },
    computed: {
        ...mapState({ overlay_type: 'overlay_type', search_keyword: 'search_keyword' }),
        isLoading() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        tableHeader() {
            return [
                {
                    text: 'Id',
                    value: 'id',
                    autoTruncate: true,
                    sortable: false,
                    width: '75%',
                },
                {
                    text: 'Type',
                    value: 'type',
                    sortable: false,
                    width: '25%',
                },
                { text: '', value: 'action', sortable: false },
            ]
        },
        initialRoutingTargets() {
            return this.formatRelationshipData(this.tableRows)
        },
        initialRoutingTargetHash() {
            return this.$help.hashMapByPath({
                arr: this.initialRoutingTargets,
                path: 'type',
            })
        },
        targetItemsHash() {
            return this.$help.hashMapByPath({ arr: this.targetItems, path: 'type' })
        },
        newRoutingTargetHash() {
            const diff = this.$help.arrOfObjsDiff({
                base: this.initialRoutingTargets,
                newArr: this.targetItems,
                idField: 'id',
            })
            const removedObjs = diff.get('removed')
            const addedObjs = diff.get('added')

            let newHash = this.$help.lodash.cloneDeep(this.initialRoutingTargetHash)
            addedObjs.forEach(obj => {
                const type = obj.type
                if (newHash[type]) newHash[type].push(obj)
                else newHash[type] = [obj]
            })
            removedObjs.forEach(obj => {
                const type = obj.type
                if (newHash[type]) newHash[type] = newHash[type].filter(item => item.id !== obj.id)
            })

            return newHash
        },
    },
    async mounted() {
        await this.$help.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        /**
         * @param {Array} arr - array of object, each object must have id and type attributes
         * @returns {Array} - returns valid relationship array data
         */
        formatRelationshipData(arr) {
            return arr.map(item => ({ id: item.id, type: item.type }))
        },
        getStatusIcon({ state, type }) {
            switch (type) {
                case 'services':
                    return this.$help.serviceStateIcon(state)
                case 'servers':
                    return this.$help.serverStateIcon(state)
                case 'monitors':
                    return this.$help.monitorStateIcon(state)
            }
        },
        emitUpdateEvt({ type, data }) {
            this.$emit('on-relationship-update', { type, data, isUpdatingRouteTarget: true })
        },
        onDelete(item) {
            this.targetItems = this.formatRelationshipData([item])
            this.dialogTitle = `${this.$t('unlink')} ${this.$tc(item.type, 1)}`
            this.isConfDlgOpened = true
        },
        confirmDelete() {
            const initialHash = this.initialRoutingTargetHash
            const hash = this.targetItemsHash
            for (const type of Object.keys(hash))
                this.emitUpdateEvt({
                    type,
                    data: this.$help.lodash.xorWith(
                        initialHash[type],
                        hash[type],
                        (a, b) => a.id === b.id
                    ),
                })
        },
        onEdit() {
            this.dialogTitle = `${this.$t(`editEntity`, { entityName: this.$t('routingTargets') })}`
            this.isRoutingTargetDlgOpened = true
        },
        confirmEdit() {
            for (const type of Object.keys(this.newRoutingTargetHash)) {
                const newData = this.newRoutingTargetHash[type]
                if (!this.$help.lodash.isEqual(this.initialRoutingTargetHash[type], newData))
                    this.emitUpdateEvt({ type, data: newData })
            }
        },
    },
}
</script>
