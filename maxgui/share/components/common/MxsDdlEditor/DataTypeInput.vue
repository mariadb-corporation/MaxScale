<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="height"
        type="select"
        name="data-type"
        required
        :error.sync="error"
        :getInputRef="() => $typy($refs, 'inputCtr').safeObject"
    >
        <v-combobox
            ref="inputCtr"
            v-model="inputValue"
            class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb',
                bottom: true,
                offsetY: true,
            }"
            :items="items"
            item-text="value"
            item-value="value"
            outlined
            dense
            :height="height"
            hide-details
            :rules="[v => !!v]"
            :return-object="false"
            :error="error"
            cache-items
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'data-type-input',
    props: {
        value: { type: String, required: true },
        height: { type: Number, required: true },
        items: { type: Array, default: () => [] },
    },
    data() {
        return {
            isInputShown: false,
            error: false,
        }
    },
    computed: {
        inputValue: {
            get() {
                return this.value
            },
            set(v) {
                if (v !== this.inputValue) this.$emit('on-input', this.$typy(v).safeString)
            },
        },
    },
    methods: {
        onBlur(e) {
            this.inputValue = this.$typy(e, 'srcElement.value').safeString
            if (this.inputValue) this.isInputShown = false
        },
    },
}
</script>
