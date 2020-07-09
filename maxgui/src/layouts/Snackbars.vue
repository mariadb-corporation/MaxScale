<template>
    <v-snackbar
        v-model="message.status"
        :color="message.type"
        :bottom="true"
        :right="true"
        :timeout="timeout"
        multi-line
    >
        <div style="width:100%" class="d-inline-flex align-center justify-center">
            <v-icon
                v-if="message.type === 'info'"
                class="mr-4 material-icons"
                size="22"
                color="white"
            >
                $vuetify.icons.statusInfo
            </v-icon>
            <v-icon v-else-if="message.type === 'error'" class="mr-4" size="22" color="white">
                $vuetify.icons.alertError
            </v-icon>
            <v-icon v-else-if="message.type === 'warning'" class="mr-4" size="22" color="white">
                $vuetify.icons.alertWarning
            </v-icon>
            <v-icon v-else class="mr-4" size="22" :color="message.type">
                $vuetify.icons.alertSuccess
            </v-icon>

            <div class="d-flex flex-column">
                <span v-for="(item, i) in message.text" :key="i">
                    {{ item }}
                </span>
            </div>
            <v-spacer />
            <v-btn dark class="ml-4 mr-0" icon @click="message.status = false">
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
 * Change Date: 2024-06-15
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
        message() {
            return this.$store.state.message
        },
    },
}
</script>
