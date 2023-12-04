<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="$attrs.height"
        :name="$attrs.name"
        :error.sync="error"
        :disabled="Boolean($attrs.disabled)"
        :required="Boolean($attrs.required)"
        :getInputRef="() => $typy($refs, 'inputCtr.$children[0]').safeObject"
        v-on="$listeners"
    >
        <mxs-debounced-field
            ref="inputCtr"
            v-model="inputValue"
            class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
            single-line
            outlined
            dense
            hide-details
            autocomplete="off"
            :rules="[v => ($attrs.required ? !!v : true)]"
            :error="error"
            v-bind="{ ...$attrs }"
            v-on="$listeners"
            @blur="onBlur"
        />
    </mxs-lazy-input>
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

export default {
    name: 'lazy-text-field',
    inheritAttrs: false,
    data() {
        return {
            isInputShown: false,
            error: false,
        }
    },
    computed: {
        inputValue: {
            get() {
                return this.$attrs.value
            },
            set(v) {
                if (v !== this.inputValue) this.$emit('on-input', v)
            },
        },
    },
    methods: {
        onBlur(e) {
            this.inputValue = this.$typy(e, 'srcElement.value').safeString
            if (this.inputValue || !this.required) this.isInputShown = false
        },
    },
}
</script>
