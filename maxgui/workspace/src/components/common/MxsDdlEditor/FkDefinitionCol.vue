<template>
    <v-text-field
        v-if="data.field === FK_EDITOR_ATTRS.NAME"
        v-model="inputValue"
        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
        :class="`${data.field}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
    />
    <v-select
        v-else
        v-model="inputValue"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="data.field"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="enumValues"
        item-text="text"
        item-value="id"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :multiple="isColumnField"
    >
        <template v-if="isColumnField" v-slot:item="{ item }">
            <v-icon class="mr-3">
                {{
                    inputValue.includes(item.id)
                        ? 'mdi-checkbox-marked'
                        : 'mdi-checkbox-blank-outline'
                }}
            </v-icon>
            {{ item.text }}
            <v-spacer />
            <span class="ma-0 ml-auto field__label mxs-color-helper text-small-text">
                {{ item.type }}
            </span>
        </template>
    </v-select>
    <!-- TODO: Add an option for REF_TARGET input to manually type in new target -->
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
/*
 *
 * data: {
 *  field?: string, header name
 *  value?:  (string | string[]), cell value
 *  rowIdx?: number, row index of the key being altered
 * }
 */
import { mapState } from 'vuex'
export default {
    name: 'fk-definition-col',
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        referencingColOptions: { type: Array, required: true },
        refTargets: { type: Array, required: true },
        refColOpts: { type: Array, required: true },
    },
    computed: {
        ...mapState({
            FK_EDITOR_ATTRS: state => state.mxsWorkspace.config.FK_EDITOR_ATTRS,
            REF_OPTS: state => state.mxsWorkspace.config.REF_OPTS,
        }),
        inputValue: {
            get() {
                return this.data.value
            },
            set(v) {
                this.$emit('on-input', { ...this.data, value: v })
            },
        },
        isColumnField() {
            return (
                this.data.field === this.FK_EDITOR_ATTRS.COLS ||
                this.data.field === this.FK_EDITOR_ATTRS.REF_COLS
            )
        },
        enumValues() {
            const { COLS, REF_TARGET, REF_COLS, ON_UPDATE, ON_DELETE } = this.FK_EDITOR_ATTRS
            switch (this.data.field) {
                case COLS:
                    return this.referencingColOptions
                case REF_TARGET:
                    return this.refTargets
                case REF_COLS:
                    return this.refColOpts
                case ON_UPDATE:
                case ON_DELETE:
                    return Object.values(this.REF_OPTS)
                default:
                    return []
            }
        },
    },
}
</script>
