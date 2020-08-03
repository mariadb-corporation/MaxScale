<template>
    <details-parameters-collapse
        v-if="currentService.attributes.parameters"
        :searchKeyWord="searchKeyWord"
        :resourceId="currentService.id"
        :parameters="currentService.attributes.parameters"
        :moduleParameters="processedModuleParameters"
        :updateResourceParameters="updateServiceParameters"
        :onEditSucceeded="onEditSucceeded"
        :loading="loadingModuleParams ? true : loading"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapActions } from 'vuex'
export default {
    name: 'parameters-table',

    props: {
        onEditSucceeded: { type: Function, required: true },
        loading: { type: Boolean, required: true },
    },

    data() {
        return {
            processedModuleParameters: [],
            loadingModuleParams: true,
        }
    },

    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            moduleParameters: 'moduleParameters',
            currentService: 'service/currentService',
        }),
    },

    async created() {
        const { router: routerId } = this.currentService.attributes
        await this.fetchModuleParameters(routerId)
        this.loadingModuleParams = true
        await this.processModuleParameters()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            updateServiceParameters: 'service/updateServiceParameters',
        }),
        async processModuleParameters() {
            if (this.moduleParameters.length) {
                this.processedModuleParameters = this.moduleParameters
                const self = this
                await this.$help.delay(150).then(() => (self.loadingModuleParams = false))
            }
        },
    },
}
</script>
