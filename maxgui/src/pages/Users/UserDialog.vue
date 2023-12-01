<template>
    <mxs-dlg v-bind="{ ...$attrs }" :saveText="type" v-on="$listeners">
        <template v-slot:form-body>
            <p
                v-if="type === USER_ADMIN_ACTIONS.DELETE"
                class="confirmations-text"
                v-html="
                    $mxs_t(`confirmations.${USER_ADMIN_ACTIONS.DELETE}`, { targetId: currUser.id })
                "
            />
            <div v-if="type === USER_ADMIN_ACTIONS.UPDATE" class="d-flex align-center mb-2">
                <v-icon size="20" class="mr-1">$vuetify.icons.mxs_user </v-icon>
                <span>{{ currUser.id }}</span>
            </div>
            <template v-if="type === USER_ADMIN_ACTIONS.UPDATE || type === USER_ADMIN_ACTIONS.ADD">
                <!-- [DOM] Password forms should have (optionally hidden) username fields for accessibility:
                     (More info: https://goo.gl/9p2vKq) -->
                <input
                    v-if="type === USER_ADMIN_ACTIONS.UPDATE"
                    hidden
                    name="username"
                    autocomplete="username"
                    :value="currUser.id"
                />
                <v-text-field
                    v-if="type === USER_ADMIN_ACTIONS.ADD"
                    v-model="currUser.id"
                    :rules="rule($mxs_t('username'))"
                    class="vuetify-input--override error--text__bottom mb-4"
                    autofocus
                    dense
                    :height="36"
                    single-line
                    outlined
                    required
                    autocomplete="username"
                    :placeholder="$mxs_t('username')"
                    hide-details="auto"
                />
                <label
                    v-if="type === USER_ADMIN_ACTIONS.UPDATE"
                    class="field__label mxs-color-helper text-small-text label-required"
                >
                    {{ $mxs_t('newPass') }}
                </label>
                <v-text-field
                    v-model="currUser.password"
                    :rules="rule($mxs_t('password'))"
                    :type="isPwdVisible ? 'text' : 'password'"
                    class="vuetify-input--override vuetify-input--override-password error--text__bottom"
                    autocomplete="new-password"
                    :autofocus="type === USER_ADMIN_ACTIONS.UPDATE"
                    single-line
                    dense
                    :height="36"
                    outlined
                    required
                    :placeholder="$mxs_t(type === USER_ADMIN_ACTIONS.ADD ? 'password' : '')"
                    hide-details="auto"
                >
                    <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                        {{ isPwdVisible ? 'mdi-eye-off' : 'mdi-eye' }}
                    </v-icon>
                </v-text-field>

                <template v-if="type === USER_ADMIN_ACTIONS.ADD">
                    <v-select
                        v-model="currUser.role"
                        :items="Object.values(USER_ROLES)"
                        outlined
                        class="vuetify-input--override v-select--mariadb error--text__bottom mt-4"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="36"
                        hide-details="auto"
                        :placeholder="$mxs_t('role')"
                        :rules="rule($mxs_t('role'))"
                    />
                </template>
            </template>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
export default {
    name: 'user-dialog',
    inheritAttrs: false,
    props: {
        type: { type: String, required: true }, // check USER_ADMIN_ACTIONS
        user: { type: Object, default: null },
    },
    data() {
        return {
            isPwdVisible: false,
        }
    },
    computed: {
        ...mapState({
            USER_ROLES: state => state.app_config.USER_ROLES,
            USER_ADMIN_ACTIONS: state => state.app_config.USER_ADMIN_ACTIONS,
        }),
        currUser: {
            get() {
                return this.user
            },
            set(value) {
                this.$emit('update:user', value)
            },
        },
    },
    methods: {
        rule(inputName) {
            return [val => !!val || this.$mxs_t('errors.requiredInput', { inputName })]
        },
    },
}
</script>
