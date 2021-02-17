<template>
    <v-snackbar
        v-model="snackbar_message.status"
        :color="snackbar_message.type"
        :bottom="true"
        :right="true"
        :timeout="timeout"
        multi-line
    >
        <div style="width:100%" class="d-inline-flex align-center justify-center">
            <v-icon
                v-if="snackbar_message.type === 'info'"
                class="mr-4 material-icons"
                size="22"
                color="background"
            >
                $vuetify.icons.statusInfo
            </v-icon>
            <v-icon
                v-else-if="snackbar_message.type === 'error'"
                class="mr-4"
                size="22"
                color="background"
            >
                $vuetify.icons.alertError
            </v-icon>
            <v-icon
                v-else-if="snackbar_message.type === 'warning'"
                class="mr-4"
                size="22"
                color="background"
            >
                $vuetify.icons.alertWarning
            </v-icon>
            <v-icon v-else class="mr-4" size="22" :color="snackbar_message.type">
                $vuetify.icons.alertSuccess
            </v-icon>

            <div class="d-flex flex-column">
                <span v-for="(item, i) in snackbar_message.text" :key="i">
                    {{ item }}
                </span>
            </div>
            <v-spacer />
            <v-btn dark class="ml-4 mr-0" icon @click="snackbar_message.status = false">
                <v-icon size="24">
                    close
                </v-icon>
            </v-btn>
        </div>
    </v-snackbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'snackbars',
    data() {
        return {
            timeout: 6000,
        }
    },
    computed: {
        snackbar_message: function() {
            return this.$store.state.snackbar_message
        },
    },
}
</script>
