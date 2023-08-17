<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="height"
        type="select"
        :disabled="isDisabled"
        :getInputRef="() => $typy($refs, 'inputCtr').safeObject"
        class="fill-height d-flex align-center"
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
            :items="items"
            outlined
            dense
            :height="height"
            hide-details
            :disabled="isDisabled"
            cache-items
            @blur="isInputShown = false"
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
    name: 'generated-input',
    props: {
        value: { type: String, required: true },
        rowData: { type: Array, required: true },
        height: { type: Number, required: true },
        items: { type: Array, default: () => [] },
    },
    data() {
        return {
            isInputShown: false,
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTRS_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTRS_IDX_MAP,
        }),
        isPK() {
            return this.$typy(this.rowData, `[${this.COL_ATTRS_IDX_MAP[this.COL_ATTRS.PK]}]`)
                .safeBoolean
        },
        isDisabled() {
            //disable if column is PK
            return this.isPK //https://mariadb.com/kb/en/generated-columns/#index-support
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
}
</script>
