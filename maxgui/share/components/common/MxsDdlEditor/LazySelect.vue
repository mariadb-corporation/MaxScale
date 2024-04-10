<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="selectionText || inputValue"
        :height="$attrs.height"
        type="select"
        :name="$attrs.name"
        :disabled="Boolean($attrs.disabled)"
        :required="Boolean($attrs.required)"
        :getInputRef="() => $typy($refs, 'inputCtr').safeObject"
        v-on="$listeners"
    >
        <v-select
            ref="inputCtr"
            v-model="inputValue"
            class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb',
                bottom: true,
                offsetY: true,
            }"
            outlined
            dense
            hide-details
            cache-items
            :rules="[v => ($attrs.required ? !!v : true)]"
            v-bind="{ ...$attrs }"
            v-on="$listeners"
            @blur="isInputShown = false"
        >
            <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                <slot :name="slot" v-bind="props" />
            </template>
        </v-select>
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
    name: 'lazy-select',
    inheritAttrs: false,
    props: {
        selectionText: { type: String, default: '' }, // custom selection text
    },
    data() {
        return {
            isInputShown: false,
        }
    },
    computed: {
        inputValue: {
            get() {
                return this.$attrs.value
            },
            set(v) {
                this.$emit('on-input', v)
            },
        },
    },
}
</script>
