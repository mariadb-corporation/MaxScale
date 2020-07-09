<template>
    <div>
        <v-btn
            :disabled="handleShowCreateBtn"
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
        <forms v-model="createDialog" :closeModal="() => (createDialog = false)" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions } from 'vuex'
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
        currentRouteName: function() {
            return this.$route.name
        },
        handleShowCreateBtn: function() {
            const matchArr = [
                'monitor',
                'monitors',
                'server',
                'servers',
                'service',
                'services',
                'session',
                'sessions',
                'filter',
                'filters',
            ]
            if (matchArr.includes(this.currentRouteName)) return false
            return true
        },
    },

    methods: {
        ...mapActions({
            fetchAllModules: 'maxscale/fetchAllModules',
        }),
        async create() {
            await this.fetchAllModules()
            this.createDialog = true
        },
    },
}
</script>
