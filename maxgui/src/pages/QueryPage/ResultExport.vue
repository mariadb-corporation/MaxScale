<template>
    <v-tooltip
        top
        transition="slide-y-transition"
        content-class="shadow-drop color text-navigation py-1 px-4"
    >
        <template v-slot:activator="{ on }">
            <v-btn
                x-small
                class="mr-2"
                outlined
                depressed
                color="accent-dark"
                v-on="on"
                @click="openConfigDialog"
            >
                <v-icon size="14" color="accent-dark">
                    file_download
                </v-icon>
            </v-btn>
            <base-dialog
                v-model="isConfigDialogOpened"
                :onSave="onExport"
                :title="$t('exportResults')"
                saveText="export"
                minBodyWidth="512px"
                :lazyValidation="false"
                @is-form-valid="isFormValid = $event"
            >
                <template v-slot:form-body>
                    <v-container class="pa-1">
                        <v-row class="my-0 mx-n1">
                            <v-col cols="12" md="12" class="pa-1">
                                <label class="field__label color text-small-text label-required">
                                    {{ $t('fileName') }}
                                </label>
                                <v-text-field
                                    v-model="fileName"
                                    class="std error--text__bottom"
                                    name="file-name"
                                    dense
                                    outlined
                                    :height="36"
                                    :rules="[
                                        val =>
                                            !!val ||
                                            $t('errors.requiredInput', { inputName: 'File name' }),
                                    ]"
                                    required
                                    hide-details="auto"
                                />
                            </v-col>

                            <v-col cols="12" md="12" class="pa-1">
                                <label class="field__label color text-small-text label-required">
                                    {{ $t('fileFormat') }}
                                </label>
                                <v-select
                                    v-model="selectedFormat"
                                    :items="fileFormats"
                                    outlined
                                    dense
                                    :height="36"
                                    class="std mariadb-select-input error--text__bottom"
                                    :menu-props="{
                                        contentClass: 'mariadb-select-v-menu',
                                        bottom: true,
                                        offsetY: true,
                                    }"
                                    return-object
                                    item-text="extension"
                                    item-value="contentType"
                                    :rules="[
                                        v =>
                                            !!v ||
                                            $t('errors.requiredInput', {
                                                inputName: 'File format',
                                            }),
                                    ]"
                                    hide-details="auto"
                                    required
                                />
                            </v-col>
                        </v-row>
                        <v-row
                            v-if="$typy(selectedFormat, 'extension').safeObject === 'csv'"
                            class="mx-n1"
                        >
                            <v-col cols="12" :md="chosenDelimiter.val ? 12 : 6" class="pa-1">
                                <label class="field__label color text-small-text">
                                    {{ $t('delimiter') }}
                                </label>
                                <v-select
                                    v-model="chosenDelimiter"
                                    return-object
                                    :items="delimiters"
                                    item-text="txt"
                                    item-value="val"
                                    outlined
                                    dense
                                    :height="36"
                                    class="std mariadb-select-input error--text__bottom"
                                    :menu-props="{
                                        contentClass: 'mariadb-select-v-menu',
                                        bottom: true,
                                        offsetY: true,
                                    }"
                                    :rules="[
                                        v =>
                                            !!v ||
                                            $t('errors.requiredInput', {
                                                inputName: 'Delimiter',
                                            }),
                                    ]"
                                    hide-details="auto"
                                    required
                                />
                            </v-col>
                            <v-col v-if="!chosenDelimiter.val" cols="12" md="6" class="pa-1">
                                <label class="field__label color text-small-text">
                                    {{ $t('custdelimiter') }}
                                </label>
                                <v-text-field
                                    v-model="custDelimiter"
                                    class="std error--text__bottom"
                                    dense
                                    outlined
                                    :height="36"
                                    :rules="[
                                        v =>
                                            !!v ||
                                            $t('errors.requiredInput', {
                                                inputName: $t('custdelimiter'),
                                            }),
                                    ]"
                                    hide-details="auto"
                                    required
                                />
                            </v-col>
                            <v-col cols="12" class="pa-1 mt-3">
                                <v-checkbox
                                    v-model="noBackslashEscapes"
                                    class="no-back-slash-escapes-checkbox pa-0 ma-0"
                                    color="primary"
                                    hide-details="auto"
                                >
                                    <template v-slot:label>
                                        <label class="v-label">
                                            {{ $t('noBackslashEscapes') }}
                                        </label>
                                        <v-tooltip
                                            top
                                            transition="slide-y-transition"
                                            content-class="shadow-drop white color text-navigation py-1 px-4"
                                        >
                                            <template v-slot:activator="{ on }">
                                                <v-icon
                                                    class="ml-1 material-icons-outlined pointer"
                                                    size="16"
                                                    color="#9DB4BB"
                                                    v-on="on"
                                                >
                                                    info
                                                </v-icon>
                                            </template>
                                            <span>{{ $t('info.noBackslashEscapes') }}</span>
                                        </v-tooltip>
                                    </template>
                                </v-checkbox>
                            </v-col>
                            <v-col cols="12" class="pa-1">
                                <v-checkbox
                                    v-model="withHeaders"
                                    class="no-back-slash-escapes-checkbox pa-0 ma-0"
                                    color="primary"
                                    :label="$t('withHeaders')"
                                    hide-details="auto"
                                />
                            </v-col>
                        </v-row>
                    </v-container>
                </template>
            </base-dialog>
        </template>
        <span>{{ $t('exportResults') }}</span>
    </v-tooltip>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'result-export',
    props: {
        rows: { type: Array, required: true },
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
            chosenDelimiter: null,
            noBackslashEscapes: false,
            custDelimiter: '',
            withHeaders: false,
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
        delimiters() {
            return [
                { txt: 'Tab', val: '\t' },
                { txt: 'Comma', val: ',' },
                { txt: 'Custom', val: '' },
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
            let delimiter = ''
            if (this.chosenDelimiter.val) delimiter = this.chosenDelimiter.val
            else delimiter = this.custDelimiter
            let str = ''
            if (this.withHeaders) {
                let headers = this.headers.map(header => this.escapeCell(header.text))
                str = `${headers.join(delimiter)}\n`
            }
            str += this.rows
                .map(row => row.map(cell => this.escapeCell(cell)).join(delimiter))
                .join('\n')

            return `${str}\n` // line terminator
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
         * @param {(String|Number)} v cell value
         * @returns {(String|Number)} returns escape value
         */
        escapeCell(v) {
            // NULL is returned as js null in the query result.
            if (this.$typy(v).isNull) return this.noBackslashEscapes ? 'NULL' : '\\N' // db escape
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
            return `MaxScale Query Results - ${this.$help.dateFormat({
                value: new Date(),
                formatType: 'DATE_RFC2822',
            })}`
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
            this.chosenDelimiter = this.delimiters[0] // tab
            this.selectedFormat = this.fileFormats[0] // csv
        },
    },
}
</script>

<style lang="scss" scoped>
.field__label-radio {
    ::v-deep.v-label {
        color: $small-text;
        font-size: $label-control-size;
    }
}
::v-deep .no-back-slash-escapes-checkbox {
    label {
        font-size: $label-control-size;
        color: $small-text;
    }
}
</style>
