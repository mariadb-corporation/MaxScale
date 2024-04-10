<template>
    <div>
        <label
            class="field__label mxs-color-helper text-small-text"
            :class="{ 'label-required': $attrs.required }"
            :for="id"
        >
            {{ label }}
        </label>
        <v-text-field
            :id="id"
            v-bind="{ ...$attrs }"
            class="vuetify-input--override error--text__bottom"
            dense
            :height="36"
            hide-details="auto"
            outlined
            :rules="$attrs.required ? rules : []"
            v-on="$listeners"
        >
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </v-text-field>
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
export default {
    name: 'mxs-label-field',
    inheritAttrs: false,
    props: {
        label: { type: String, required: true },
        customErrMsg: { type: String, default: '' },
    },
    computed: {
        id() {
            if (this.$attrs.id) return this.$attrs.id
            return `mxs-label-field-${this.$helpers.lodash.uniqueId()}`
        },
        customRules() {
            return this.$typy(this.$attrs, 'rules').safeArray
        },
        rules() {
            if (this.customRules.length) return this.customRules
            else
                return [
                    val =>
                        !!val ||
                        this.customErrMsg ||
                        this.$mxs_t('errors.requiredInput', { inputName: this.label }),
                ]
        },
    },
}
</script>
