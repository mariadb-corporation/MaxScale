<template>
    <mxs-collapse
        :toggleOnClick="() => (showInputs = !showInputs)"
        :isContentVisible="showInputs"
        wrapperClass="tbl-opts px-1 pt-2"
        :title="title"
    >
        <template v-slot:arrow-toggle="{ toggleOnClick, isContentVisible }">
            <v-btn icon small class="arrow-toggle" @click="toggleOnClick">
                <v-icon
                    :class="[isContentVisible ? 'rotate-down' : 'rotate-right']"
                    size="28"
                    color="navigation"
                >
                    mdi-chevron-down
                </v-icon>
            </v-btn>
        </template>
        <v-container fluid class="py-0 px-1 pb-3">
            <v-row class="ma-0">
                <v-col cols="12" :md="isCreating ? 4 : 6" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('name') }}
                    </label>
                    <v-text-field
                        id="table-name"
                        v-model="tblOpts.name"
                        :rules="requiredRule($mxs_t('name'))"
                        required
                        name="table-name"
                        :height="28"
                        class="vuetify-input--override error--text__bottom"
                        hide-details="auto"
                        dense
                        outlined
                    />
                </v-col>
                <v-col v-if="isCreating" cols="12" md="4" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_tc('schemas', 1) }}
                    </label>
                    <v-combobox
                        v-model="tblOpts.schema"
                        :items="schemas"
                        outlined
                        dense
                        :height="28"
                        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        hide-details="auto"
                        :rules="requiredRule($mxs_tc('schemas', 1))"
                    >
                        <template v-slot:prepend-inner>
                            <slot name="prepend-inner" />
                        </template>
                    </v-combobox>
                </v-col>
                <v-col cols="12" :md="isCreating ? 4 : 6" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text">
                        {{ $mxs_t('comment') }}
                    </label>
                    <v-text-field
                        v-model="tblOpts.comment"
                        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
                        single-line
                        outlined
                        dense
                        :height="28"
                        hide-details="auto"
                    />
                </v-col>
            </v-row>
            <v-row class="ma-0">
                <v-col cols="6" md="4" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('engine') }}
                    </label>
                    <v-select
                        v-model="tblOpts.engine"
                        :items="engines"
                        name="table-engine"
                        outlined
                        class="vuetify-input--override v-select--mariadb error--text__bottom"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="28"
                        hide-details="auto"
                    />
                </v-col>
                <v-col cols="6" md="4" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('charset') }}
                    </label>
                    <charset-collate-select
                        v-model="tblOpts.charset"
                        :items="Object.keys(charsetCollationMap)"
                        :defItem="defDbCharset"
                        :height="28"
                        :rules="requiredRule($mxs_t('charset'))"
                        @input="onSelectCharset"
                    />
                </v-col>
                <v-col cols="6" md="4" class="py-0 px-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('collation') }}
                    </label>
                    <charset-collate-select
                        v-model="tblOpts.collation"
                        :items="
                            $typy(charsetCollationMap, `[${tblOpts.charset}].collations`).safeArray
                        "
                        :defItem="defCollation"
                        :height="28"
                        :rules="requiredRule($mxs_t('collation'))"
                    />
                </v-col>
            </v-row>
        </v-container>
    </mxs-collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import CharsetCollateSelect from '@wsSrc/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
export default {
    name: 'table-opts',
    components: { CharsetCollateSelect },
    props: {
        value: { type: Object, required: true },
        engines: { type: Array, required: true },
        defDbCharset: { type: String, required: true },
        charsetCollationMap: { type: Object, required: true },
        schemas: { type: Array, default: () => [] },
        isCreating: { type: Boolean, required: true },
    },
    data() {
        return {
            showInputs: true,
        }
    },
    computed: {
        title() {
            if (this.isCreating) return this.$mxs_t('createTbl')
            return this.$mxs_t('alterTbl')
        },
        tblOpts: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        defCollation() {
            return this.$typy(this.charsetCollationMap, `[${this.tblOpts.charset}].defCollation`)
                .safeString
        },
    },
    methods: {
        onSelectCharset() {
            // Use default collation of selected charset
            this.tblOpts.collation = this.defCollation
        },
        requiredRule(inputName) {
            return [val => !!val || this.$mxs_t('errors.requiredInput', { inputName })]
        },
    },
}
</script>

<style lang="scss">
.tbl-opts {
    .mxs-collapse-title {
        font-size: 0.75rem !important;
    }
}
</style>
