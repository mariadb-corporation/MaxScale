<template>
    <mxs-tooltip-btn
        btnClass="mr-2"
        x-small
        outlined
        depressed
        color="primary"
        @click="openConfigDialog"
    >
        <template v-slot:btn-content>
            <v-icon size="14">mdi-download</v-icon>
            <mxs-dlg
                v-model="isConfigDialogOpened"
                :onSave="onExport"
                :title="$mxs_t('exportResults')"
                saveText="export"
                minBodyWidth="512px"
                :lazyValidation="false"
                @is-form-valid="isFormValid = $event"
            >
                <template v-slot:form-body>
                    <v-container class="pa-1">
                        <v-row class="ma-n1">
                            <v-col cols="12" md="12" class="pa-1">
                                <label
                                    class="field__label mxs-color-helper text-small-text label-required"
                                >
                                    {{ $mxs_t('fileName') }}
                                </label>
                                <v-text-field
                                    v-model="fileName"
                                    class="vuetify-input--override error--text__bottom"
                                    name="file-name"
                                    dense
                                    outlined
                                    :height="36"
                                    :rules="[
                                        val =>
                                            !!val ||
                                            $mxs_t('errors.requiredInput', {
                                                inputName: 'File name',
                                            }),
                                    ]"
                                    required
                                    hide-details="auto"
                                />
                            </v-col>

                            <v-col cols="12" md="12" class="pa-1">
                                <label
                                    class="field__label mxs-color-helper text-small-text label-required"
                                >
                                    {{ $mxs_t('fileFormat') }}
                                </label>
                                <v-select
                                    v-model="selectedFormat"
                                    :items="fileFormats"
                                    outlined
                                    dense
                                    :height="36"
                                    class="vuetify-input--override v-select--mariadb error--text__bottom"
                                    :menu-props="{
                                        contentClass: 'v-select--menu-mariadb',
                                        bottom: true,
                                        offsetY: true,
                                    }"
                                    return-object
                                    item-text="extension"
                                    item-value="contentType"
                                    :rules="[
                                        v =>
                                            !!v ||
                                            $mxs_t('errors.requiredInput', {
                                                inputName: 'File format',
                                            }),
                                    ]"
                                    hide-details="auto"
                                    required
                                />
                            </v-col>
                        </v-row>
                        <template v-if="$typy(selectedFormat, 'extension').safeObject === 'csv'">
                            <v-row class="ma-n1">
                                <v-col
                                    v-for="(_, key) in csvTerminatedOpts"
                                    :key="key"
                                    cols="12"
                                    md="12"
                                    class="pa-1"
                                >
                                    <label class="field__label mxs-color-helper text-small-text">
                                        {{ $mxs_t(key) }}
                                    </label>
                                    <v-text-field
                                        v-model="csvTerminatedOpts[key]"
                                        class="vuetify-input--override error--text__bottom"
                                        dense
                                        outlined
                                        :height="36"
                                        :rules="[
                                            v =>
                                                !!v ||
                                                $mxs_t('errors.requiredInput', {
                                                    inputName: $mxs_t(key),
                                                }),
                                        ]"
                                        hide-details="auto"
                                        required
                                    />
                                </v-col>
                            </v-row>
                            <v-row class="mt-3 mx-n1 mb-n1">
                                <v-col
                                    v-for="(_, key) in csvCheckboxOpts"
                                    :key="key"
                                    cols="12"
                                    class="pa-1"
                                >
                                    <v-checkbox
                                        v-model="csvCheckboxOpts[key]"
                                        class="pa-0 ma-0 v-checkbox--mariadb"
                                        color="primary"
                                        hide-details="auto"
                                    >
                                        <template v-slot:label>
                                            <label class="v-label pointer">
                                                {{ $mxs_t(key) }}
                                            </label>
                                            <v-tooltip
                                                v-if="key === 'noBackslashEscapes'"
                                                top
                                                transition="slide-y-transition"
                                                max-width="400"
                                            >
                                                <template v-slot:activator="{ on }">
                                                    <v-icon
                                                        class="ml-1 pointer"
                                                        size="16"
                                                        color="info"
                                                        v-on="on"
                                                    >
                                                        mdi-information-outline
                                                    </v-icon>
                                                </template>
                                                <span>{{ $mxs_t(`info.${key}`) }}</span>
                                            </v-tooltip>
                                        </template>
                                    </v-checkbox>
                                </v-col>
                            </v-row>
                        </template>
                    </v-container>
                </template>
            </mxs-dlg>
        </template>
        {{ $mxs_t('exportResults') }}
    </mxs-tooltip-btn>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'result-export',
    props: {
        rows: { type: Array, required: true },
        defExportFileName: { type: String, required: true },
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
    },
    data() {
        return {
            isFormValid: false,
            isConfigDialogOpened: false,
            selectedFormat: null,
            fileName: '',
            // csv export options
            csvTerminatedOpts: {
                fieldsTerminatedBy: '',
                linesTerminatedBy: '',
            },
            csvCheckboxOpts: {
                noBackslashEscapes: false,
                withHeaders: false,
            },
        }
    },
    computed: {
        fileFormats() {
            return [
                {
                    contentType: 'data:text/csv;charset=utf-8;',
                    extension: 'csv',
                },
                {
                    contentType: 'data:application/json;charset=utf-8;',
                    extension: 'json',
                },
            ]
        },
        jsonData() {
            let arr = []
            for (let i = 0; i < this.rows.length; ++i) {
                let obj = {}
                for (const [n, header] of this.headers.entries()) {
                    obj[`${header.text}`] = this.rows[i][n]
                }
                arr.push(obj)
            }
            return JSON.stringify(arr)
        },
        csvData() {
            let fieldsTerminatedBy = this.unescapedUserInput(
                this.csvTerminatedOpts.fieldsTerminatedBy
            )
            let linesTerminatedBy = this.unescapedUserInput(
                this.csvTerminatedOpts.linesTerminatedBy
            )
            let str = ''
            if (this.csvCheckboxOpts.withHeaders) {
                let headers = this.headers.map(header => this.escapeCell(header.text))
                str = `${headers.join(fieldsTerminatedBy)}${linesTerminatedBy}`
            }
            str += this.rows
                .map(row => row.map(cell => this.escapeCell(cell)).join(fieldsTerminatedBy))
                .join(linesTerminatedBy)

            return `${str}${linesTerminatedBy}`
        },
    },
    watch: {
        isConfigDialogOpened(v) {
            if (v) this.assignDefOpt()
            else Object.assign(this.$data, this.$options.data())
        },
    },
    methods: {
        /**
         * Input entered by the user is escaped automatically.
         * As the result, if the user enters \t, it is escaped as \\t. However, here
         * we allow the user to add the custom line | fields terminator, so when the user
         * enters \t, it should be parsed as a tab character. At the moment, JS doesn't
         * allow to have dynamic escaped char. So this function uses JSON.parse approach
         * to unescaped inputs
         * @param {String} v - users utf8 input
         */
        unescapedUserInput(v) {
            try {
                let str = v
                // if user enters \\, escape it again so it won't be removed when it is parsed by JSON.parse
                if (str.includes('\\\\')) str = this.escapeCell(str)
                return JSON.parse(
                    '"' +
                    str.replace(/"/g, '\\"') + // escape " to prevent json syntax errors
                        '"'
                )
            } catch (e) {
                this.$logger.error(e)
            }
        },
        /**
         * @param {(String|Number)} v cell value
         * @returns {(String|Number)} returns escape value
         */ escapeCell(v) {
            // NULL is returned as js null in the query result.
            if (this.$typy(v).isNull)
                return this.csvCheckboxOpts.noBackslashEscapes ? 'NULL' : '\\N' // db escape
            if (this.$typy(v).isString) return v.replace(/\\/g, '\\\\') // replace \ with \\
            return v
        },
        getData(fileExtension) {
            switch (fileExtension) {
                case 'json':
                    return this.jsonData
                case 'csv':
                    return this.csvData
            }
        },
        getDefFileName() {
            return `${this.defExportFileName} - ${this.$helpers.dateFormat({ value: new Date() })}`
        },
        openConfigDialog() {
            this.isConfigDialogOpened = !this.isConfigDialogOpened
            this.fileName = this.getDefFileName()
        },
        onExport() {
            const { contentType, extension } = this.selectedFormat
            let a = document.createElement('a')
            a.href = `${contentType},${encodeURIComponent(this.getData(extension))}`
            a.download = `${this.fileName}.${extension}`
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
        },
        assignDefOpt() {
            //TODO: Determine OS newline and store it as user preferences
            // escape reserved single character escape sequences so it can be rendered to the DOM
            this.csvTerminatedOpts = {
                fieldsTerminatedBy: '\\t',
                linesTerminatedBy: '\\n',
            }
            this.selectedFormat = this.fileFormats[0] // csv
        },
    },
}
</script>
