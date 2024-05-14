<template>
    <v-snackbar
        v-model="msgObj.status"
        :color="msgObj.type"
        :bottom="true"
        :right="true"
        :timeout="6000"
        multi-line
    >
        <div style="width:100%" class="d-inline-flex align-center justify-center">
            <v-icon
                v-if="msgObj.type === 'info'"
                class="mr-4 material-icons"
                size="22"
                color="white"
            >
                $vuetify.icons.mxs_statusInfo
            </v-icon>
            <v-icon v-else-if="msgObj.type === 'error'" class="mr-4" size="22" color="white">
                $vuetify.icons.mxs_alertError
            </v-icon>
            <v-icon v-else-if="msgObj.type === 'warning'" class="mr-4" size="22" color="white">
                $vuetify.icons.mxs_alertWarning
            </v-icon>
            <v-icon v-else class="mr-4" size="22" :color="msgObj.type">
                $vuetify.icons.mxs_alertSuccess
            </v-icon>

            <div class="d-flex flex-column">
                <span v-for="(item, i) in msgObj.text" :key="i">
                    {{ item }}
                </span>
            </div>
            <v-spacer />
            <v-btn class="ml-4 mr-0" icon @click="msgObj.status = false">
                <v-icon size="24">
                    mdi-close
                </v-icon>
            </v-btn>
        </div>
    </v-snackbar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'snackbars',
    props: {
        msgObj: { type: Object, required: true },
    },
}
</script>
