<template>
    <div>
        <v-select
            v-model="chosenConn"
            :items="connOptions"
            name="resourceName"
            outlined
            dense
            class="std mariadb-select-input error--text__bottom"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            hide-details
            :rules="[v => !!v || $t('errors.requiredInput', { inputName: 'This field' })]"
            required
            placeholder="Select connection"
        >
            <template v-slot:selection="{ item }">
                <div class="v-select__selection v-select__selection--comma">
                    {{ item }}
                </div>
            </template>
            <template v-slot:item="{ item, on, attrs }">
                <div class="v-list-item__title d-flex align-center" v-bind="attrs" v-on="on">
                    <div
                        v-if="item !== newConnOption"
                        class="d-flex align-center flex-row flex-grow-1"
                    >
                        {{ item }}
                        <v-tooltip
                            bottom
                            transition="slide-y-transition"
                            content-class="shadow-drop color text-navigation py-1 px-4"
                        >
                            <template v-slot:activator="{ on }">
                                <v-btn
                                    class="ml-auto"
                                    icon
                                    v-on="on"
                                    @click.prevent="() => disconnect({ showSnackbar: true })"
                                >
                                    <v-icon size="20" color="error">
                                        $vuetify.icons.unlink
                                    </v-icon>
                                </v-btn>
                            </template>
                            <span>Disconnect</span>
                        </v-tooltip>
                    </div>
                    <div v-else class="text-decoration-underline color text-primary">
                        {{ item }}
                    </div>
                </div>
            </template>
        </v-select>
        <connection-dialog
            v-model="isConnDialogOpened"
            :handleSave="handleOpenConn"
            :onCancel="assignActiveConn"
            :onClose="assignActiveConn"
        />
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
            chosenConn: [],
            newConnOption: 'New Connection',
        }
    },
    computed: {
        ...mapState({
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
        }),
        connOptions() {
            let options = [this.newConnOption]
            if (this.curr_cnct_resource) options.unshift(this.curr_cnct_resource.name)
            return options
        },
    },
    watch: {
        chosenConn(v) {
            if (v === this.newConnOption) this.openConnDialog()
        },
        curr_cnct_resource(v) {
            if (v) this.chosenConn = v.name
        },
    },
    mounted() {
        // auto open connection-dialog when there is no active opened connection
        if (this.active_conn_state) this.assignActiveConn()
        else this.openConnDialog()
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
        }),
        assignActiveConn() {
            if (this.curr_cnct_resource) this.chosenConn = this.curr_cnct_resource.name
            else this.chosenConn = ''
        },
        openConnDialog() {
            this.isConnDialogOpened = true
        },
        async handleOpenConn(body) {
            await this.openConnect(body)
        },
    },
}
</script>
