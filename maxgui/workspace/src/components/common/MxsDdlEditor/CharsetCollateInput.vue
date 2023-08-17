<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="height"
        :disabled="isDisabled"
        type="select"
        :getInputRef="() => $typy($refs, 'inputCtr.$children[0]').safeObject"
        class="fill-height d-flex align-center"
    >
        <charset-collate-select
            ref="inputCtr"
            v-model="inputValue"
            :items="
                isCharsetInput
                    ? Object.keys(charsetCollationMap)
                    : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
            "
            :defItem="isCharsetInput ? defTblCharset : defTblCollation"
            :disabled="isDisabled"
            :height="height"
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
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import CharsetCollateSelect from '@wsSrc/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
import { checkCharsetSupport } from '@wsSrc/components/common/MxsDdlEditor/utils'

export default {
    name: 'charset-collate-input',
    components: { CharsetCollateSelect },
    props: {
        value: { type: String, required: true },
        rowData: { type: Array, required: true },
        field: { type: String, required: true },
        height: { type: Number, required: true },
        charsetCollationMap: { type: Object, required: true },
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
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
        columnCharset() {
            return this.$typy(this.rowData, `[${this.COL_ATTRS_IDX_MAP[this.COL_ATTRS.CHARSET]}]`)
                .safeString
        },
        columnType() {
            return this.$typy(this.rowData, `[${this.COL_ATTRS_IDX_MAP[this.COL_ATTRS.TYPE]}]`)
                .safeString
        },
        isCharsetInput() {
            return this.field === this.COL_ATTRS.CHARSET
        },
        isDisabled() {
            if (this.columnType.includes('NATIONAL')) return true
            return !checkCharsetSupport(this.columnType)
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
            this.inputValue = e.srcElement.value
            this.isInputShown = false
        },
    },
}
</script>
