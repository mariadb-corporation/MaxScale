<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="router"
            :objType="MXS_OBJ_TYPES.SERVICES"
            v-bind="moduleParamsProps"
        />
        <collapsible-ctr class="mt-4" titleWrapperClass="mx-n9" :title="$mxs_t('routingTargets')">
            <routing-target-select
                v-model="routingTargetItems"
                :defaultItems="defRoutingTargetItems"
            />
        </collapsible-ctr>
        <resource-relationships
            ref="filtersRelationship"
            relationshipsType="filters"
            :items="filtersList"
            :defaultItems="defFilterItem"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from '@src/components/ObjectForms/ModuleParameters'
import ResourceRelationships from '@src/components/ObjectForms/ResourceRelationships'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'service-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        allFilters: { type: Array, required: true },
        defRoutingTargetItems: { type: Array, default: () => [] },
        defFilterItem: { type: Array, default: () => [] },
        moduleParamsProps: { type: Object, required: true },
    },
    data() {
        return {
            routingTargetItems: [],
        }
    },
    computed: {
        filtersList() {
            return this.allFilters.map(({ id, type }) => ({ id, type }))
        },
        routingTargetRelationships() {
            let data = this.routingTargetItems
            if (this.$typy(this.routingTargetItems).isObject) data = [this.routingTargetItems]
            return data.reduce((obj, item) => {
                if (!obj[item.type]) obj[item.type] = { data: [] }
                obj[item.type].data.push(item)
                return obj
            }, {})
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        getValues() {
            const { moduleInputs, filtersRelationship } = this.$refs
            const { moduleId, parameters } = moduleInputs.getModuleInputValues()
            return {
                moduleId,
                parameters,
                relationships: {
                    filters: { data: filtersRelationship.getSelectedItems() },
                    ...this.routingTargetRelationships,
                },
            }
        },
    },
}
</script>
