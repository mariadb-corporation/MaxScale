<template>
    <v-row>
        <v-col cols="12" class="pa-1">
            <h3
                class="mxs-color-helper text-navigation font-weight-light"
                :style="{ lineHeight: '36px' }"
            >
                {{ $mxs_t('destination') }}
            </h3>
            <p v-if="!allServers.length" class="mxs-color-helper text-error mt-2">
                {{ $mxs_t('noEntityAvailable', { entityName: $mxs_tc(destTargetType, 2) }) }}
            </p>
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
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <db-input v-model.trim="dest.db" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <uid-input v-model.trim="dest.user" name="db-user" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <pwd-input v-model.trim="dest.password" name="db-password" />
        </v-col>
    </v-row>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import UidInput from './UidInput.vue'
import PwdInput from './PwdInput.vue'
import DbInput from './DbInput.vue'

export default {
    name: 'etl-dest-conn',
    components: { UidInput, PwdInput, DbInput },
    props: {
        /**
         * @property {string} user
         * @property {string} password
         * @property {string} db
         * @property {string} target
         */
        value: { type: Object, required: true },
        allServers: { type: Array, required: true },
        destTargetType: { type: String, required: true },
    },
    computed: {
        dest: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
