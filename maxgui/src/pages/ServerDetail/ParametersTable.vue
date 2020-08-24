<template>
    <details-parameters-collapse
        :resourceId="current_server.id"
        :parameters="current_server.attributes.parameters"
        usePortOrSocket
        :updateResourceParameters="updateServerParameters"
        :onEditSucceeded="onEditSucceeded"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
export default {
    name: 'parameters-table',

    props: {
        onEditSucceeded: { type: Function, required: true },
    },

    computed: {
        ...mapState({
            current_server: state => state.server.current_server,
        }),
    },

    async created() {
        await this.fetchModuleParameters('servers')
    },
    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            updateServerParameters: 'server/updateServerParameters',
        }),
    },
}
</script>
