<template>
    <v-col cols="12" md="6">
        <b>{{ $mxs_t('source') }}</b>
        <p v-if="!drivers.length" class="mxs-color-helper text-error mt-2">
            {{ $mxs_t('errors.noDriversFound') }}
        </p>
        <v-row v-else class="my-0 mx-n1">
            <v-col cols="12" md="6" class="pa-1">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t('driver') }}
                </label>
                <v-select
                    v-model="src.driver"
                    :items="drivers"
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
                    :placeholder="$mxs_t('selectOdbcDriver')"
                    :rules="[
                        v =>
                            !!v ||
                            $mxs_t('errors.requiredInput', {
                                inputName: $mxs_t('driver'),
                            }),
                    ]"
                    hide-details="auto"
                />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <db-input
                    v-model.trim="src.db"
                    :required="shouldRequireDb"
                    :customErrMsg="$mxs_t('errors.requiredDb')"
                    :validate-on-blur="true"
                />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <mxs-txt-field-with-label
                    v-model.trim="src.server"
                    :label="$mxs_t('hostname/IP')"
                    :required="true"
                />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <mxs-txt-field-with-label
                    v-model.trim="src.port"
                    :label="$mxs_t('port')"
                    :required="true"
                />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <uid-input v-model.trim="src.user" />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <pwd-input v-model.trim="src.password" />
            </v-col>
        </v-row>
    </v-col>
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
    name: 'etl-src-conn',
    components: { UidInput, PwdInput, DbInput },
    props: {
        /**
         * @property {string} driver
         * @property {string} server
         * @property {number} port
         * @property {string} user
         * @property {string} password
         * @property {string} db
         */
        value: { type: Object, required: true },
        drivers: { type: Array, required: true },
    },
    computed: {
        src: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        driverMap() {
            return this.$helpers.lodash.keyBy(this.drivers, 'id')
        },
        shouldRequireDb() {
            const driver = this.driverMap[this.src.driver]
            /**
             * PostgreSQL requires defining a database when creating a connection
             * There could be either an ANSI or Unicode version of PostgreSQL ODBC driver, so
             * checking the `driver` path if it includes `psqlodbc` would probably be enough to
             * know it's a PostgreSQL driver.
             */
            if (this.$typy(driver, 'attributes.driver').safeString.includes('psqlodbc')) return true
            return false
        },
    },
}
</script>
