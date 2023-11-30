<template>
    <mxs-dlg v-bind="{ ...$attrs }" :hasChanged="hasChanged" v-on="$listeners">
        <template v-slot:form-body>
            <routing-target-select
                v-model="selectedItems"
                :routerId="routerId"
                :defRoutingTarget="defRoutingTarget"
                :defaultItems="defaultItems"
                hasPaddingBetweenInputs
                @has-changed="hasChanged = $event"
            />
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
        initialRoutingTargetHash: { type: Object, required: true },
    },
    data() {
        return {
            hasChanged: false,
            defRoutingTarget: '',
            selectedItems: [],
            defaultItems: [],
        }
    },
    computed: {
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
            if (v) this.assignDefRoutingTargets()
            else Object.assign(this.$data, this.$options.data())
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
        assignDefRoutingTargets() {
            this.defRoutingTarget = this.initialRoutingTarget || 'servers'
            const initialItems = [].concat(...Object.values(this.initialRoutingTargetHash))
            this.defaultItems =
                this.defRoutingTarget === 'cluster'
                    ? this.$typy(initialItems, '[0]').safeObjectOrEmpty
                    : initialItems
        },
    },
}
</script>
