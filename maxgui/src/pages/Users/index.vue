<template>
    <page-wrapper>
        <portal to="page-header">
            <div class="d-flex align-center">
                <div class="d-inline-flex align-center">
                    <h4
                        style="line-height: normal;"
                        class="mb-0 color text-navigation text-h4 text-capitalize"
                    >
                        {{ $route.name }}
                    </h4>
                </div>
            </div>
        </portal>
        <portal to="page-header--right">
            <div class="d-flex align-center">
                <global-search class="mr-4" />
                <v-btn
                    width="160"
                    outlined
                    height="36"
                    rounded
                    class="text-capitalize px-8 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
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
                    <template v-slot:actions="{ data: { item } }">
                        <v-tooltip
                            v-for="action in Object.values(userAdminActions).filter(
                                item => item.type !== USER_ADMIN_ACTIONS.ADD
                            )"
                            :key="action.text"
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop color text-navigation py-1 px-4"
                        >
                            <template v-slot:activator="{ on }">
                                <v-btn
                                    icon
                                    v-on="on"
                                    @click="actionHandler({ type: action.type, user: item })"
                                >
                                    <v-icon size="18" :color="action.color">
                                        {{ action.icon }}
                                    </v-icon>
                                </v-btn>
                            </template>
                            <span>{{ action.text }}</span>
                        </v-tooltip>
                    </template>
                    <template v-for="h in tableHeaders" v-slot:[`${h.value}`]="{ data: { item } }">
                        <span
                            :key="h.value"
                            :class="{
                                'font-weight-bold': logged_in_user.name === item.id, // Bold active user
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
import { mapState, mapActions, mapGetters } from 'vuex'
import UserDialog from './UserDialog'
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
            USER_ADMIN_ACTIONS: state => state.app_config.USER_ADMIN_ACTIONS,
            logged_in_user: state => state.user.logged_in_user,
        }),
        ...mapGetters({ getUserAdminActions: 'user/getUserAdminActions' }),
        tableRows() {
            let rows = []
            for (const user of this.all_inet_users) {
                const { id, type, attributes: { account } = {} } = user
                rows.push({ id, role: account, type })
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
        this.setTableHeight()
        await this.fetchAllNetworkUsers()
    },
    methods: {
        ...mapActions({
            fetchAllNetworkUsers: 'user/fetchAllNetworkUsers',
            manageInetUser: 'user/manageInetUser',
        }),
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
                title: this.$t(`userOps.actions.${type}`),
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
