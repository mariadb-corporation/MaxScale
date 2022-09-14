<template>
    <div>
        <label class="field__label color text-small-text d-block">
            {{ $t('selectRoutingTargets') }}
        </label>
        <v-select
            v-model="chosenTarget"
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
            v-on="$listeners"
        />
    </div>
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
has-changed: value: Boolean
*/
import { mapActions } from 'vuex'
export default {
    name: 'routing-target-select',
    props: {
        value: { type: [Array, Object], required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
        routingTarget: { type: String, default: '' }, // sync
        routerId: { type: String, default: '' }, // the id of the MaxScale object being altered
    },
    data() {
        return {
            allTargetsMap: [],
            itemsList: [],
        }
    },
    computed: {
        selectedItems: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        chosenTarget: {
            get() {
                return this.routingTarget
            },
            set(v) {
                this.$emit('update:routingTarget', v)
            },
        },
        allowMultiple() {
            return this.chosenTarget !== 'cluster'
        },
        routingTargets() {
            return [
                { txt: 'servers and services', value: 'targets' },
                { txt: 'servers', value: 'servers' },
                { txt: 'cluster', value: 'cluster' },
            ]
        },
        entityName() {
            switch (this.chosenTarget) {
                case 'targets':
                    return 'items'
                case 'cluster':
                    return 'clusters'
                default:
                    return this.chosenTarget || ''
            }
        },
        specifyRoutingTargetsLabel() {
            switch (this.chosenTarget) {
                case 'targets':
                    return this.$tc('specifyRoutingTarget', 2, [this.$tc(this.entityName, 2)])
                case 'servers':
                    return this.$tc('specifyRoutingTarget', 2, [this.$tc(this.entityName, 2)])
                default:
                    return this.$tc('specifyRoutingTarget', 1, [this.$tc(this.entityName, 1)])
            }
        },
        chosenRelationshipTypes() {
            let types = []
            switch (this.chosenTarget) {
                case 'targets':
                    types = ['services', 'servers']
                    break
                case 'cluster':
                    types = ['monitors']
                    break
                case 'servers':
                    types = ['servers']
                    break
            }
            return types
        },
    },
    watch: {
        chosenTarget: {
            handler() {
                this.itemsList = this.chosenRelationshipTypes.reduce((arr, type) => {
                    arr = [...arr, ...this.allTargetsMap[type]]
                    return arr
                }, [])
            },
        },
    },
    async created() {
        this.allTargetsMap = await this.getAllTargetsMap()
    },
    methods: {
        ...mapActions({ getResourceState: 'getResourceState' }),
        async getAllTargetsMap() {
            let map = {}
            let relationshipTypes = ['services', 'servers', 'monitors']
            for (const type of relationshipTypes) {
                const data = await this.getResourceState({
                    resourceType: type,
                    caller: 'routing-target-select-getAllTargetsMap',
                })
                if (!map[type]) map[type] = []
                map[type] = [
                    ...map[type],
                    ...data.reduce((arr, item) => {
                        // cannot target the service itself
                        if (item.id !== this.routerId) arr.push({ id: item.id, type: item.type })
                        return arr
                    }, []),
                ]
            }
            return map
        },
        onChangeRoutingTarget() {
            this.selectedItems = []
        },
    },
}
</script>
