<template>
    <div>
        <!-- TODO: add delete option to disconnect -->
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
        >
            <template v-slot:selection="{ item }">
                <div class="v-select__selection v-select__selection--comma">
                    {{ item }}
                </div>
            </template>
            <template v-slot:item="{ item, on, attrs }">
                <div class="v-list-item__title" v-bind="attrs" v-on="on">
                    <span v-if="item !== newConnOption">{{ item }}</span>
                    <span v-else class="text-decoration-underline color text-primary">
                        {{ item }}
                    </span>
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
    props: {
        hasActiveConn: { type: Boolean, required: true },
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
            curr_cnct_resource_name: state => state.query.curr_cnct_resource_name,
        }),
        connOptions() {
            return [this.curr_cnct_resource_name, this.newConnOption]
        },
    },
    watch: {
        chosenConn(v) {
            if (v === this.newConnOption) this.openConnDialog()
        },
        curr_cnct_resource_name(v) {
            this.chosenConn = v
        },
    },
    mounted() {
        // auto open connection-dialog when there is no active opened connection
        if (!this.hasActiveConn) this.openConnDialog()
        if (this.curr_cnct_resource_name) this.assignActiveConn()
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
        }),
        assignActiveConn() {
            this.chosenConn = this.curr_cnct_resource_name
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
