<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="height"
        :error.sync="error"
        :required="isRequired"
        :getInputRef="() => $typy($refs, 'inputCtr.$children[0]').safeObject"
        class="fill-height d-flex align-center"
    >
        <mxs-debounced-field
            ref="inputCtr"
            v-model="inputValue"
            class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
            single-line
            outlined
            dense
            :height="height"
            hide-details
            :required="isRequired"
            :rules="[v => (isRequired ? !!v : true)]"
            :error="error"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'txt-input',
    props: {
        value: { type: String, default: undefined },
        field: { type: String, required: true },
        height: { type: Number, required: true },
    },
    data() {
        return {
            isInputShown: false,
            error: false,
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
        }),
        isRequired() {
            return this.field === this.COL_ATTRS.NAME
        },
        inputValue: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('on-input', v)
            },
        },
    },
    methods: {
        onBlur(e) {
            this.inputValue = this.$typy(e, 'srcElement.value').safeString
            if (this.inputValue || !this.isRequired) this.isInputShown = false
        },
    },
}
</script>
