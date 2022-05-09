<template>
    <v-toolbar
        outlined
        elevation="0"
        height="45"
        class="wke-toolbar"
        :class="{ 'ml-0': is_fullscreen }"
    >
        <connection-manager :disabled="getIsConnBusy" />
    </v-toolbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// TODO: Get rid of this component, the functionalities in this component will be handled later in a new UI design,
import { mapState, mapGetters } from 'vuex'
import ConnectionManager from './ConnectionManager'

export default {
    name: 'worksheet-toolbar',
    components: { ConnectionManager },
    computed: {
        ...mapState({ is_fullscreen: state => state.wke.is_fullscreen }),
        ...mapGetters({ getIsConnBusy: 'queryConn/getIsConnBusy' }),
    },
}
</script>

<style lang="scss" scoped>
::v-deep.wke-toolbar {
    .v-toolbar__content {
        padding: 4px 12px;
    }
}
</style>
