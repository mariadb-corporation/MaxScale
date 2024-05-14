<template>
    <v-row class="ma-n1">
        <v-col cols="12" class="pa-1">
            <h3 class="mxs-stage-ctr__title mxs-color-helper text-navigation font-weight-light">
                {{ $mxs_t('destination') }}
            </h3>
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <label
                class="field__label mxs-color-helper text-small-text text-capitalize label-required"
            >
                {{ $mxs_tc(destTargetType, 1) }}
            </label>
            <v-select
                v-model="dest.target"
                :items="allServers"
                item-text="id"
                item-value="id"
                name="driver"
                outlined
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                hide-details="auto"
                :placeholder="$mxs_tc('select', 1, { entityName: $mxs_tc(destTargetType, 1) })"
                :rules="[
                    v =>
                        !!v ||
                        $mxs_t('errors.requiredInput', {
                            inputName: $mxs_tc(destTargetType, 1),
                        }),
                ]"
                :error-messages="
                    allServers.length
                        ? ''
                        : $mxs_t('noEntityAvailable', { entityName: $mxs_tc(destTargetType, 2) })
                "
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-timeout-input v-model.number="dest.timeout" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-uid-input v-model.trim="dest.user" name="db-user" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-pwd-input v-model.trim="dest.password" name="db-password" />
        </v-col>
    </v-row>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'etl-dest-conn',
    props: {
        value: { type: Object, required: true },
        allServers: { type: Array, required: true },
        destTargetType: { type: String, required: true },
    },
    data() {
        return {
            dest: { user: '', password: '', timeout: 30, target: '' },
        }
    },
    watch: {
        dest: {
            immediate: true,
            deep: true,
            handler(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
