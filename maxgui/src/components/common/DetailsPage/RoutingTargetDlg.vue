<template>
    <base-dialog v-bind="{ ...$attrs }" :hasChanged="hasChanged" v-on="$listeners">
        <template v-slot:form-body>
            <!-- TODO: Create a reusable component for these inputs and use it in the `+ Create New` btn for service -->
            <label class="field__label color text-small-text d-block">
                {{ $t('selectRoutingTargets') }}
            </label>
            <v-select
                v-model="routingTarget"
                :items="routingTargets"
                item-text="txt"
                item-value="value"
                outlined
                dense
                :height="36"
                class="std mariadb-select-input error--text__bottom"
                :menu-props="{
                    contentClass: 'mariadb-select-v-menu',
                    bottom: true,
                    offsetY: true,
                }"
                :rules="[v => !!v || $t('errors.requiredInput', { inputName: 'This field' })]"
                required
                @change="onChangeRoutingTarget"
            />
            <label class="mt-4 field__label color text-small-text d-block">
                {{ specifyRoutingTargetsLabel }}
            </label>
            <select-dropdown
                v-model="selectedItems"
                :items="itemsList"
                :entityName="entityName"
                :multiple="allowMultiple"
                :defaultItems="defaultItems"
                :showPlaceHolder="false"
                @has-changed="hasChanged = $event"
            />
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits
@selected-items: value: Array
*/
export default {
    name: 'routing-target-dlg',
    inheritAttrs: false,
    props: {
        routerId: { type: String, default: '' }, // the id of the MaxScale object being altered
        getRelationshipData: { type: Function, required: true },
        initialRoutingTargetHash: { type: Object, required: true },
    },
    data() {
        return {
            routingTarget: '',
            hasChanged: false,
            itemsList: [],
            // selectedItems and defaultItems either array or object depends on allowMultiple
            selectedItems: [],
            defaultItems: [],
        }
    },
    computed: {
        routingTargets() {
            return [
                { txt: 'servers and services', value: 'targets' },
                { txt: 'servers', value: 'servers' },
                { txt: 'cluster', value: 'cluster' },
            ]
        },
        allowMultiple() {
            return this.routingTarget !== 'cluster'
        },
        specifyRoutingTargetsLabel() {
            switch (this.routingTarget) {
                case 'targets':
                    return this.$tc('specifyRoutingTarget', 2, [this.$tc(this.entityName, 2)])
                case 'servers':
                    return this.$tc('specifyRoutingTarget', 2, [this.$tc(this.entityName, 2)])
                default:
                    return this.$tc('specifyRoutingTarget', 1, [this.$tc(this.entityName, 1)])
            }
        },
        entityName() {
            switch (this.routingTarget) {
                case 'targets':
                    return 'items'
                case 'cluster':
                    return 'clusters'
                default:
                    return this.routingTarget || ''
            }
        },
        relationshipTypes() {
            let relationshipTypes = []
            switch (this.routingTarget) {
                case 'targets':
                    relationshipTypes = ['services', 'servers']
                    break
                case 'cluster':
                    relationshipTypes = ['monitors']
                    break
                case 'servers':
                    relationshipTypes = ['servers']
                    break
            }
            return relationshipTypes
        },
        // Detect initial routing target
        initialRoutingTarget() {
            const types = Object.keys(this.initialRoutingTargetHash)
            const isTargetingCluster = types.includes('monitors')
            const isTargetingServers = types.includes('servers')
            const isTargetingServices = types.includes('services')

            if (isTargetingCluster) return 'cluster'
            else if ((isTargetingServers && isTargetingServices) || isTargetingServices)
                return 'targets'
            else if (isTargetingServers) return 'servers'
            return ''
        },
    },
    watch: {
        async '$attrs.value'(v) {
            if (v) {
                this.routingTarget = this.initialRoutingTarget || 'servers'
                this.handleAssignDefItems()
            } else Object.assign(this.$data, this.$options.data())
        },
        routingTarget: {
            async handler() {
                this.itemsList = await this.getItemLists()
            },
        },
        selectedItems: {
            deep: true,
            handler(v) {
                if (this.$typy(v).isArray) this.$emit('selected-items', v)
                else this.$emit('selected-items', [v])
            },
        },
    },

    methods: {
        async getItemLists() {
            let availableItems = []
            for (const type of this.relationshipTypes) {
                const data = await this.getRelationshipData(type)
                availableItems = [
                    ...availableItems,
                    ...data.reduce((arr, item) => {
                        // cannot target the service itself
                        if (item.id !== this.routerId) arr.push({ id: item.id, type: item.type })
                        return arr
                    }, []),
                ]
            }
            return availableItems
        },
        handleAssignDefItems() {
            const initialItems = [].concat(...Object.values(this.initialRoutingTargetHash))
            this.defaultItems = this.allowMultiple
                ? initialItems
                : this.$typy(initialItems, '[0]').safeObjectOrEmpty
        },
        onChangeRoutingTarget(v) {
            if (v === this.initialRoutingTarget) this.handleAssignDefItems()
            else this.selectedItems = []
        },
    },
}
</script>
