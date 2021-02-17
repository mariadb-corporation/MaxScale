<template>
    <v-select
        v-model="selectedItems"
        :items="items"
        item-text="id"
        return-object
        :multiple="multiple"
        :name="entityName"
        :clearable="clearable"
        outlined
        dense
        class="std mariadb-select-input"
        :class="[required && 'error--text__bottom']"
        :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
        :placeholder="
            showPlaceHolder
                ? $tc('select', multiple ? 2 : 1, {
                      entityName: $tc(entityName, multiple ? 2 : 1),
                  })
                : ''
        "
        :no-data-text="
            $tc('noEntityAvailable', multiple ? 2 : 1, {
                entityName: $tc(entityName, multiple ? 2 : 1),
            })
        "
        :rules="rules.requiredField"
        :hide-details="!required"
        @change="onChange"
    >
        <template v-slot:selection="{ item, index }">
            <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
                {{ item.id }}
            </span>
            <span
                v-if="index === 1"
                class="v-select__selection v-select__selection--comma color caption text-field-text "
            >
                (+{{ selectedItems.length - 1 }} {{ $t('others') }})
            </span>
        </template>
    </v-select>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits two events:
@get-selected-items always return Array regardless multiple props is true or false
@has-changed: value = boolean
*/
export default {
    name: 'select-dropdown',
    props: {
        entityName: { type: String, required: true },
        items: { type: Array, required: true },
        multiple: { type: Boolean, default: false },
        clearable: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        defaultItems: { type: [Array, Object], default: () => [] },
        showPlaceHolder: { type: Boolean, default: true },
    },
    data() {
        return {
            selectedItems: [],
            rules: {
                requiredField: [val => this.validateRequired(val)],
            },
        }
    },
    computed: {
        // compare default value with new values
        hasChanged: function() {
            let isEqual = this.$help.lodash.isEqual(this.selectedItems, this.defaultItems)
            return !isEqual
        },
    },

    watch: {
        defaultItems: function(val) {
            this.selectedItems = val
            this.onChange(val)
        },
    },

    methods: {
        //always return array
        onChange(val) {
            this.$emit('has-changed', this.hasChanged)
            let value = val
            if (val && !this.multiple) value = [val]
            // val is undefined when items are cleared by clearable props in v-select
            else if (val === undefined) value = []
            this.$emit('get-selected-items', value)
        },
        validateRequired(val) {
            if ((val === undefined || val.length === 0) && this.required) {
                return `${this.$tc(this.entityName, this.multiple ? 2 : 1)} is required`
            }
            return true
        },
    },
}
</script>
