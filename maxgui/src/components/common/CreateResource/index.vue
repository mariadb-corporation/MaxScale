<template>
    <div>
        <v-btn
            width="160"
            outlined
            height="36"
            rounded
            class="color text-accent-dark text-capitalize px-8 font-weight-medium"
            depressed
            small
            @click.native="create"
        >
            + {{ $t('createNew') }}
        </v-btn>
        <forms v-model="createDialog" :closeModal="handleClose" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'
import Forms from './Forms'
export default {
    name: 'create-resource',
    components: {
        Forms,
    },

    data() {
        return {
            createDialog: false,
        }
    },
    computed: {
        ...mapState({
            form_type: 'form_type',
        }),
    },
    watch: {
        form_type: async function(val) {
            if (val) await this.create()
            else this.handleClose()
        },
    },
    methods: {
        ...mapActions({
            fetchAllModules: 'maxscale/fetchAllModules',
        }),
        ...mapMutations({
            SET_FORM_TYPE: 'SET_FORM_TYPE',
        }),
        async create() {
            await this.fetchAllModules()
            this.createDialog = true
        },
        handleClose() {
            if (this.form_type) this.SET_FORM_TYPE(null)
            else this.createDialog = false
        },
    },
}
</script>
