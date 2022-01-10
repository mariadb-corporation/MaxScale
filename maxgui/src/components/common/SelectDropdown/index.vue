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
        :height="36"
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
        :error-messages="errorMessages"
    >
        <template v-slot:selection="{ item, index }">
            <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
                {{ item.id }}
            </span>
            <span
                v-if="index === 1"
                class="v-select__selection v-select__selection--comma color text-caption text-field-text "
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
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits an event:
@has-changed: value = boolean
*/
export default {
    name: 'select-dropdown',
    props: {
        value: {
            validator(value) {
                return typeof value === 'object' || Array.isArray(value)
            },
            default: () => [],
            required: true,
        },
        defaultItems: { type: [Array, Object], default: () => [] },
        items: { type: Array, required: true },
        entityName: { type: String, required: true },
        multiple: { type: Boolean, default: false },
        clearable: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        showPlaceHolder: { type: Boolean, default: true },
        errorMessages: { type: String, default: '' },
    },
    data() {
        return {
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
        selectedItems: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
    },

    watch: {
        defaultItems: {
            deep: true,
            handler(val) {
                this.selectedItems = val
            },
        },
        hasChanged(v) {
            this.$emit('has-changed', v)
        },
    },

    methods: {
        validateRequired(val) {
            // val is null when items are cleared by clearable props in v-select
            if ((val === null || val.length === 0) && this.required) {
                return `${this.$tc(this.entityName, this.multiple ? 2 : 1)} is required`
            }
            return true
        },
    },
}
</script>
