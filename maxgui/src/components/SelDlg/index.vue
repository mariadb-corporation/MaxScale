<template>
    <mxs-dlg :hasChanged="hasChanged" v-bind="{ ...$attrs }" v-on="$listeners">
        <template v-slot:form-body>
            <p class="select-label">
                {{ $mxs_tc('specify', multiple ? 2 : 1) }}
                {{ $mxs_tc(entityName, multiple ? 2 : 1) }}
            </p>
            <mxs-select
                v-model="selectedItems"
                :defaultItems="defaultItems"
                :items="itemsList"
                :entityName="entityName"
                :multiple="multiple"
                :clearable="clearable"
                :showPlaceHolder="false"
                @has-changed="hasChanged = $event"
            />
            <slot name="body-append"></slot>
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits two events
@selected-items: value: Array
@on-open: event triggered after dialog is opened
*/
export default {
    name: 'sel-dlg',
    inheritAttrs: false,
    props: {
        entityName: { type: String, required: true },
        clearable: { type: Boolean, default: false },
        multiple: { type: Boolean, default: false },
        itemsList: { type: Array, required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
    },
    data() {
        return {
            selectedItems: [],
            hasChanged: false,
        }
    },
    watch: {
        '$attrs.value'(v) {
            if (v) this.$emit('on-open')
            else this.selectedItems = []
        },
        selectedItems: {
            deep: true,
            handler(v) {
                if (this.$typy(v).isNull) this.$emit('selected-items', [])
                else if (this.$typy(v).isArray) this.$emit('selected-items', v)
                else this.$emit('selected-items', [v])
            },
        },
    },
}
</script>
