<template>
    <page-wrapper class="fill-height">
        <portal to="page-header">
            <div class="d-flex align-center">
                <div class="d-inline-flex align-center">
                    <h4
                        style="line-height: normal;"
                        class="mb-0 mxs-color-helper text-navigation text-h4 text-capitalize"
                    >
                        {{ $route.name }}
                    </h4>
                </div>
            </div>
        </portal>
        <portal to="page-header--right">
            <div class="d-flex align-center">
                <global-search />
                <v-btn
                    v-if="isAdmin"
                    width="160"
                    outlined
                    height="36"
                    rounded
                    class="ml-4 text-capitalize px-8 font-weight-medium"
                    depressed
                    small
                    color="primary"
                    @click="actionHandler({ type: USER_ADMIN_ACTIONS.ADD })"
                >
                    + {{ userAdminActions[USER_ADMIN_ACTIONS.ADD].text }}
                </v-btn>
            </div>
        </portal>
        <v-sheet class="d-flex flex-column fill-height mt-12">
            <div ref="tableWrapper" class="fill-height">
                <data-table
                    :headers="tableHeaders"
                    :data="tableRows"
                    :search="search_keyword"
                    sortBy="id"
                    showActionsOnHover
                    showAll
                    :height="tableHeight"
                    fixedHeader
                >
                    <template v-if="isAdmin" v-slot:actions="{ data: { item } }">
                        <mxs-tooltip-btn
                            v-for="action in [
                                userAdminActions[USER_ADMIN_ACTIONS.UPDATE],
                                ...(isLoggedInUser(item)
                                    ? []
                                    : [userAdminActions[USER_ADMIN_ACTIONS.DELETE]]),
                            ]"
                            :key="action.text"
                            icon
                            :color="action.color"
                            @click="actionHandler({ type: action.type, user: item })"
                        >
                            <template v-slot:btn-content>
                                <v-icon size="18"> {{ action.icon }} </v-icon>
                            </template>
                            {{ action.text }}
                        </mxs-tooltip-btn>
                    </template>
                    <template v-for="h in tableHeaders" v-slot:[`${h.value}`]="{ data: { item } }">
                        <span
                            :key="h.value"
                            v-mxs-highlighter="{ keyword: search_keyword, txt: item[h.value] }"
                            :class="{
                                'font-weight-bold': isLoggedInUser(item), // Bold active user
                            }"
                        >
                            {{ item[h.value] }}
                        </span>
                    </template>
                </data-table>
            </div>
            <user-dialog
                v-model="userDlg.isOpened"
                :title="userDlg.title"
                :type="userDlg.type"
                :user.sync="userDlg.user"
                :onSave="confirmSave"
            />
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions, mapGetters } from 'vuex'
import UserDialog from './UserDialog'
import { USER_ADMIN_ACTIONS } from '@src/constants'

export default {
    components: {
        'user-dialog': UserDialog,
    },
    data() {
        return {
            tableHeaders: [
                { text: 'Username', value: 'id' },
                { text: 'Role', value: 'role' },
                { text: 'Type', value: 'type' },
                { text: 'Created', value: 'created' },
                { text: 'Last Updated', value: 'last_update' },
                { text: 'Last Login', value: 'last_login' },
            ],
            tableHeight: 0,
            userDlg: {
                isOpened: false,
                title: '',
                type: '',
                user: { id: '', password: '', role: '' },
            },
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_inet_users: state => state.user.all_inet_users,
            logged_in_user: state => state.user.logged_in_user,
        }),
        ...mapGetters({
            getUserAdminActions: 'user/getUserAdminActions',
            isAdmin: 'user/isAdmin',
        }),
        tableRows() {
            let rows = []
            for (const user of this.all_inet_users) {
                const {
                    id,
                    type,
                    attributes: { account, created, last_update, last_login } = {},
                } = user
                rows.push({
                    id,
                    role: account,
                    type,
                    created: created ? this.$helpers.dateFormat({ value: created }) : '',
                    last_update: last_update
                        ? this.$helpers.dateFormat({ value: last_update })
                        : '',
                    last_login: last_login ? this.$helpers.dateFormat({ value: last_login }) : '',
                })
            }
            return rows
        },
        userAdminActions() {
            return this.getUserAdminActions({ scope: this })
        },
    },
    watch: {
        'userDlg.isOpened'(v) {
            // clear userDlg data when dlg is closed
            if (!v) this.userDlg = this.$options.data().userDlg
        },
    },
    async created() {
        this.USER_ADMIN_ACTIONS = USER_ADMIN_ACTIONS
        this.setTableHeight()
        await this.fetchAllNetworkUsers()
    },
    methods: {
        ...mapActions({
            fetchAllNetworkUsers: 'user/fetchAllNetworkUsers',
            manageInetUser: 'user/manageInetUser',
        }),
        isLoggedInUser(item) {
            return this.$typy(this.logged_in_user, 'name').safeString === item.id
        },
        setTableHeight() {
            this.$nextTick(() => {
                const tableHeight = this.$typy(this.$refs, 'tableWrapper.clientHeight').safeNumber
                if (tableHeight) this.tableHeight = tableHeight - 4 // 4px offset
            })
        },
        /**
         * @param {String} param.type - delete||update||add
         * @param {Object} param.user - user object
         */
        actionHandler({ type, user }) {
            this.userDlg = {
                isOpened: true,
                type,
                title: this.$mxs_t(`userOps.actions.${type}`),
                user: { ...this.userDlg.user, ...user },
            }
        },
        async confirmSave() {
            await this.manageInetUser({
                mode: this.userDlg.type,
                ...this.userDlg.user,
                callback: this.fetchAllNetworkUsers,
            })
        },
    },
}
</script>

<style lang="scss" scoped>
.cooperative-indicator {
    font-size: 0.75rem;
}
</style>
