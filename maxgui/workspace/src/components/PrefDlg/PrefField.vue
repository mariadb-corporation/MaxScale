<template>
    <div :id="field.name" class="pref-field" :class="{ 'pb-4': type !== 'boolean' }">
        <label
            v-if="type !== 'boolean'"
            class="field__label mxs-color-helper text-small-text label-required"
        >
            {{ $mxs_t(field.name) }}
        </label>
        <v-icon
            v-if="field.icon && type !== 'boolean'"
            size="14"
            :color="field.iconColor"
            class="ml-1 mb-1 pointer"
            @mouseenter="showInfoTooltip({ ...field, activator: `#${field.name}` })"
            @mouseleave="rmInfoTooltip"
        >
            {{ field.icon }}
        </v-icon>
        <row-limit-ctr
            v-if="field.name === 'rowLimit'"
            :height="36"
            hide-details="auto"
            class="vuetify-input--override error--text__bottom rowLimit"
            @change="inputValue = $event"
        />
        <v-select
            v-else-if="type === 'enum'"
            v-model="inputValue"
            :items="field.enumValues"
            outlined
            class="vuetify-input--override v-select--mariadb error--text__bottom"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb',
                bottom: true,
                offsetY: true,
            }"
            dense
            hide-details="auto"
        >
            <template v-slot:selection="{ item }">
                <div class="v-select__selection v-select__selection--comma text-capitalize">
                    {{ $mxs_tc(item, 1) }}
                </div>
            </template>
            <template v-slot:item="{ item }">
                <v-list-item-title class="text-capitalize">
                    {{ $mxs_tc(item, 1) }}
                </v-list-item-title>
            </template>
        </v-select>
        <v-checkbox
            v-else-if="type === 'boolean'"
            v-model="inputValue"
            class="v-checkbox--mariadb"
            dense
            color="primary"
            hide-details="auto"
        >
            <template v-slot:label>
                <label class="v-label pointer">{{ $mxs_t(field.name) }}</label>
                <v-icon
                    v-if="field.icon"
                    class="ml-1 material-icons-outlined pointer"
                    size="16"
                    :color="field.iconColor"
                    @mouseenter="showInfoTooltip({ ...field, activator: `#${field.name}` })"
                    @mouseleave="rmInfoTooltip"
                >
                    {{ field.icon }}
                </v-icon>
            </template>
        </v-checkbox>
        <v-text-field
            v-else-if="type === 'number'"
            v-model.number="inputValue"
            type="number"
            :rules="[
                v =>
                    validatePositiveNumber({
                        v,
                        inputName: $mxs_t(field.name),
                    }),
            ]"
            class="vuetify-input--override error--text__bottom"
            dense
            :height="36"
            hide-details="auto"
            outlined
            required
            :suffix="field.suffix"
            @keypress="$helpers.preventNonNumericalVal($event)"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import RowLimitCtr from '@wkeComps/QueryEditor/RowLimitCtr.vue'
export default {
    name: 'pref-field',
    components: { RowLimitCtr },
    props: {
        value: { type: [String, Number, Boolean], required: true },
        field: { type: Object, required: true },
        type: { type: String, required: true },
    },
    computed: {
        inputValue: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
    },
    methods: {
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName })
            if (v > 0) return true
            return false
        },
        showInfoTooltip(data) {
            this.$emit('tooltip', data)
        },
        rmInfoTooltip() {
            this.$emit('tooltip', undefined)
        },
    },
}
</script>
