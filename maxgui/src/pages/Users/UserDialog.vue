<template>
    <base-dialog v-bind="{ ...$attrs }" :saveText="type" v-on="$listeners">
        <template v-slot:form-body>
            <p
                v-if="type === USER_ADMIN_ACTIONS.DELETE"
                class="confirmations-text"
                v-html="$t(`confirmations.${USER_ADMIN_ACTIONS.DELETE}`, { targetId: currUser.id })"
            />
            <div v-if="type === USER_ADMIN_ACTIONS.UPDATE" class="d-flex align-center mb-2">
                <v-icon size="20" class="mr-1">$vuetify.icons.user </v-icon>
                <span>{{ currUser.id }}</span>
            </div>
            <template v-if="type === USER_ADMIN_ACTIONS.UPDATE || type === USER_ADMIN_ACTIONS.ADD">
                <v-text-field
                    v-if="type === USER_ADMIN_ACTIONS.ADD"
                    v-model="currUser.id"
                    :rules="rule($t('username'))"
                    class="std error--text__bottom mb-4"
                    autofocus
                    dense
                    :height="36"
                    single-line
                    outlined
                    required
                    autocomplete="username"
                    :placeholder="$t('username')"
                    hide-details="auto"
                />
                <label
                    v-if="type === USER_ADMIN_ACTIONS.UPDATE"
                    class="field__label color text-small-text label-required"
                >
                    {{ $t('newPass') }}
                </label>
                <v-text-field
                    v-model="currUser.password"
                    :rules="rule($t('password'))"
                    :type="isPwdVisible ? 'text' : 'password'"
                    class="std std-password error--text__bottom"
                    autocomplete="new-password"
                    :autofocus="type === USER_ADMIN_ACTIONS.UPDATE"
                    single-line
                    dense
                    :height="36"
                    outlined
                    required
                    :placeholder="$t(type === USER_ADMIN_ACTIONS.ADD ? 'password' : '')"
                    hide-details="auto"
                >
                    <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                        {{ isPwdVisible ? 'visibility_off' : 'visibility' }}
                    </v-icon>
                </v-text-field>

                <template v-if="type === USER_ADMIN_ACTIONS.ADD">
                    <v-select
                        v-model="currUser.role"
                        :items="Object.values(USER_ROLES)"
                        outlined
                        class="std mariadb-select-input error--text__bottom mt-4"
                        :menu-props="{
                            contentClass: 'mariadb-select-v-menu',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="36"
                        hide-details="auto"
                        :placeholder="$t('role')"
                        :rules="rule($t('role'))"
                    />
                </template>
            </template>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-29
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
            return [val => !!val || this.$t('errors.requiredInput', { inputName })]
        },
    },
}
</script>
