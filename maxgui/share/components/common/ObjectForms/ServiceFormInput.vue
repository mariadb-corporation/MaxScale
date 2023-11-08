<template>
    <div class="mb-2">
        <module-parameters ref="moduleInputs" moduleName="router" :modules="modules" />
        <mxs-collapse
            wrapperClass="mt-4"
            titleWrapperClass="mx-n9"
            :toggleOnClick="() => (showRoutingTargetInputs = !showRoutingTargetInputs)"
            :isContentVisible="showRoutingTargetInputs"
            :title="$mxs_t('routingTargets')"
        >
            <routing-target-select
                v-model="routingTargetItems"
                :defaultItems="defRoutingTargetItems"
            />
        </mxs-collapse>
        <resource-relationships
            ref="filtersRelationship"
            relationshipsType="filters"
            :items="filtersList"
            :defaultItems="defaultFilterItems"
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
import ModuleParameters from '@share/components/common/ObjectForms/ModuleParameters'
import ResourceRelationships from '@share/components/common/ObjectForms/ResourceRelationships'

export default {
    name: 'service-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        modules: { type: Array, required: true },
        allFilters: { type: Array, required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
    },
    data() {
        return {
            defaultFilterItems: [],
            //routing-target-select states
            showRoutingTargetInputs: true,
            routingTargetItems: [],
            defRoutingTargetItems: [],
        }
    },
    computed: {
        filtersList() {
            return this.allFilters.map(({ id, type }) => ({ id, type }))
        },
        hasDefServerItems() {
            return this.$typy(this.defaultItems, '[0].type').safeString === 'servers'
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
    watch: {
        defaultItems: {
            deep: true,
            handler() {
                if (this.hasDefServerItems) {
                    this.defRoutingTargetItems = this.defaultItems
                } else this.defaultFilterItems = this.defaultItems
            },
        },
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
