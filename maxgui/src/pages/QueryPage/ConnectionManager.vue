<template>
    <div>
        <v-btn
            outlined
            height="36"
            max-width="160"
            text
            class="ml-4 text-none px-2 font-weight-regular"
            depressed
            small
            color="accent-dark"
            @click="curr_cnct_resource ? null : openConnDialog()"
        >
            <div
                id="curr_cnct_resource"
                class="d-inline-block text-truncate"
                :style="{ maxWidth: `${curr_cnct_resource ? 102 : 138}px` }"
            >
                <v-icon v-if="curr_cnct_resource" class="mr-2" size="16" color="accent-dark">
                    $vuetify.icons.server
                </v-icon>
                {{ curr_cnct_resource ? curr_cnct_resource.name : $t('openConnection') }}
            </div>
            <v-tooltip
                v-if="curr_cnct_resource"
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        class="ml-2"
                        icon
                        small
                        v-on="on"
                        @click.prevent="() => disconnect({ showSnackbar: true })"
                    >
                        <v-icon size="18" color="error">
                            $vuetify.icons.unlink
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('disconnect') }}</span>
            </v-tooltip>
        </v-btn>
        <v-tooltip
            v-if="curr_cnct_resource"
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
            activator="#curr_cnct_resource"
        >
            <span>{{ $t('connectedTo') }}: {{ curr_cnct_resource.name }} </span>
        </v-tooltip>

        <connection-dialog v-model="isConnDialogOpened" :handleSave="handleOpenConn" />
    </div>
</template>

<script>
import { mapActions, mapState } from 'vuex'
import ConnectionDialog from './ConnectionDialog'
export default {
    name: 'connection-manager',
    components: {
        ConnectionDialog,
    },
    data() {
        return {
            isConnDialogOpened: false,
        }
    },
    computed: {
        ...mapState({
            checking_active_conn: state => state.query.checking_active_conn,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
        }),
    },
    watch: {
        checking_active_conn(v) {
            // after finish checking active connection
            if (!v && !this.active_conn_state)
                // auto open connection-dialog when there is no active opened connection
                this.openConnDialog()
        },
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
        }),
        openConnDialog() {
            this.isConnDialogOpened = true
        },
        async handleOpenConn(body) {
            await this.openConnect(body)
        },
    },
}
</script>
