<template>
    <div>
        <label class="field__label mxs-color-helper text-small-text d-block">
            {{ $mxs_t('selectRoutingTargets') }}
        </label>
        <v-select
            v-model="chosenTarget"
            :items="routingTargets"
            item-text="txt"
            item-value="value"
            outlined
            dense
            :height="36"
            class="vuetify-input--override v-select--mariadb error--text__bottom"
            :menu-props="{ contentClass: 'v-select--menu-mariadb', bottom: true, offsetY: true }"
            @change="onChangeRoutingTarget"
        />
        <label
            class="field__label mxs-color-helper text-small-text d-block"
            :class="{ 'mt-2': hasPaddingBetweenInputs }"
        >
            {{ specifyRoutingTargetsLabel }}
        </label>
        <mxs-select
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
This component emits
has-changed: value: Boolean
*/
import { mapActions } from 'vuex'
export default {
    name: 'routing-target-select',
    props: {
        value: { type: [Array, Object], required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
        defRoutingTarget: { type: String, default: 'servers' },
        routerId: { type: String, default: '' }, // the id of the MaxScale object being altered
        hasPaddingBetweenInputs: { type: Boolean, default: false },
    },
    data() {
        return {
            allTargetsMap: {},
            itemsList: [],
            chosenTarget: '',
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
                    return this.$mxs_tc('specifyRoutingTarget', 2, [
                        this.$mxs_tc(this.entityName, 2),
                    ])
                case 'servers':
                    return this.$mxs_tc('specifyRoutingTarget', 2, [
                        this.$mxs_tc(this.entityName, 2),
                    ])
                default:
                    return this.$mxs_tc('specifyRoutingTarget', 1, [
                        this.$mxs_tc(this.entityName, 1),
                    ])
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
                this.assignItemList()
            },
        },
        defRoutingTarget: {
            immediate: true,
            handler(v) {
                if (v) this.chosenTarget = v
            },
        },
    },
    async created() {
        this.allTargetsMap = await this.getAllTargetsMap()
        this.assignItemList()
    },
    methods: {
        ...mapActions({ getResourceData: 'getResourceData' }),
        async getAllTargetsMap() {
            let map = {}
            let relationshipTypes = ['services', 'servers', 'monitors']
            for (const type of relationshipTypes) {
                const data = await this.getResourceData({ type, fields: ['id'] })
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
        assignItemList() {
            this.itemsList = this.chosenRelationshipTypes.reduce((arr, type) => {
                arr.push(...this.$typy(this.allTargetsMap, `${[type]}`).safeArray)
                return arr
            }, [])
        },
        onChangeRoutingTarget() {
            this.selectedItems = []
        },
    },
}
</script>
